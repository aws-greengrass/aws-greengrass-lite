#define RAPIDJSON_HAS_STDSTRING 1

#include "log_manager.hpp"
#include "futures.hpp"
#include <fstream>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <temp_module.hpp>
#include <aws/crt/auth/Credentials.h>
#include <ctime>
#include <thread>
#include <chrono>

const auto LOG = ggapi::Logger::of("LogManager");

class SignWaiter
{
public:
    SignWaiter() : m_lock(), m_signal(), m_done(false) {}

    void OnSigningComplete(const std::shared_ptr<Aws::Crt::Http::HttpRequest> &, int)
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_done = true;
        m_signal.notify_one();
    }

    void Wait()
    {
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_signal.wait(lock, [this]() { return m_done == true; });
        }
    }

private:
    std::mutex m_lock;
    std::condition_variable m_signal;
    bool m_done;
};

bool LogManager::onInitialize(ggapi::Struct data) {
    std::ignore = util::getDeviceSdkApiHandle();
    // TODO: retrieve and process system config
    std::unique_lock guard{_mutex};
    _nucleus = data.getValue<ggapi::Struct>({"nucleus"});
    _system = data.getValue<ggapi::Struct>({"system"});
    LOG.atInfo().log("Initializing log manager");
    return true;
}

bool LogManager::onStart(ggapi::Struct data) {
    LOG.atInfo().log("Beginning persistent logging loop logic");
    while(true) {
        retrieveCredentialsFromTES();
        if (_credentials.hasKey("Response")) {
            LOG.atInfo().log("Credentials successfully retrieved from TES");
            LogManager::processLogsAndUpload();
        } else {
            LOG.atError().log("Could not retrieve credentials from TES");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(UPLOAD_FREQUENCY));
    }
}

bool LogManager::onStop(ggapi::Struct data) {
    return true;
}

bool LogManager::onError_stop(ggapi::Struct data) {
    return true;
}

void LogManager::retrieveCredentialsFromTES() {
    auto request{ggapi::Struct::create()};
    request.put("test", "some-unique-token");
    LOG.atInfo().log("Calling topic to request credentials from TES");
    auto tesFuture = ggapi::Subscription::callTopicFirst(
        ggapi::Symbol{TES_REQUEST_TOPIC}, request);
    if(tesFuture) {
        _credentials = ggapi::Struct(tesFuture.waitAndGetValue());
    }
    else {
        _credentials = {};
    }
}

void LogManager::createLogGroup(const std::string& logGroupName) {
    LOG.atDebug().log("Begin create log group logic");
    auto allocator = Aws::Crt::DefaultAllocator();
    std::string uriAsString = "https://logs." + _logGroup.region + ".amazonaws.com/";
    aws_io_library_init(allocator);

    // Create Credentials to pass to SigV4 signer
    auto responseCred = _credentials.get<std::string>("Response");
    auto responseBuff = ggapi::Buffer::create().put(0, std::string_view{responseCred}).fromJson();
    auto responseStruct = ggapi::Struct{responseBuff};
    auto accessKey = responseStruct.get<std::string>("AccessKeyId");
    auto secretAccessKey = responseStruct.get<std::string>("SecretAccessKey");
    auto token = responseStruct.get<std::string>("Token");
    auto expiration = responseStruct.get<std::string>("Expiration");

    // We use maximum expiration timeout as a temporary method to avoid complex parsing of the expiration result
    // from TES. If the credentials are expired, this will still show up in logs as long as HTTP response body is
    // logged.
    auto credentialsForRequest = Aws::Crt::MakeShared<Aws::Crt::Auth::Credentials>(
            allocator,
            aws_byte_cursor_from_c_str(accessKey.c_str()),
            aws_byte_cursor_from_c_str(secretAccessKey.c_str()),
            aws_byte_cursor_from_c_str(token.c_str()),
            UINT64_MAX
    );

    // SigV4 signer
    auto signer = Aws::Crt::MakeShared<Aws::Crt::Auth::Sigv4HttpRequestSigner>(allocator, allocator);
    Aws::Crt::Auth::AwsSigningConfig signingConfig(allocator);
    signingConfig.SetRegion(_logGroup.region.c_str());
    signingConfig.SetSigningAlgorithm(Aws::Crt::Auth::SigningAlgorithm::SigV4);
    signingConfig.SetSignatureType(Aws::Crt::Auth::SignatureType::HttpRequestViaHeaders);
    signingConfig.SetService("logs");
    signingConfig.SetSigningTimepoint(Aws::Crt::DateTime::Now());
    signingConfig.SetCredentials(credentialsForRequest);

    // Setup Connection TLS
    Aws::Crt::Io::TlsContextOptions tlsCtxOptions =
        Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
    Aws::Crt::Io::TlsContext tlsContext(
        tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    if(tlsContext.GetInitializationError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create TLS context");
        throw std::runtime_error("Failed to create TLS context");
    }
    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    Aws::Crt::ByteCursor urlCursor = Aws::Crt::ByteCursorFromCString(uriAsString.c_str());
    Aws::Crt::Io::Uri uri(urlCursor, allocator);

    auto hostName = uri.GetHostName();
    tlsConnectionOptions.SetServerName(hostName);

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(TIME_OUT_MS);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    if(eventLoopGroup.LastError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create event loop group");
        throw std::runtime_error("Failed to create event loop group");
    }
    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    if(defaultHostResolver.LastError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create default host resolver");
        throw std::runtime_error("Failed to create default host resolver");
    }
    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    if(clientBootstrap.LastError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create client bootstrap");
        throw std::runtime_error("Failed to create client bootstrap");
    }
    clientBootstrap.EnableBlockingShutdown();

    std::shared_ptr<Aws::Crt::Http::HttpClientConnection> connection(nullptr);
    bool errorOccurred = true;
    bool connectionShutdown = false;

    //TODO: Check reuse of variables
    std::condition_variable conditionalVar;
    std::mutex semaphoreLock;

    auto onConnectionSetup =
        [&](const std::shared_ptr<Aws::Crt::Http::HttpClientConnection> &newConnection,
            int errorCode) {
            util::TempModule tempModule{getModule()};
            std::lock_guard<std::mutex> lockGuard(semaphoreLock);
            if(!errorCode) {
                LOG.atInfo().log("Successful on establishing connection.");
                connection = newConnection;
                errorOccurred = false;
            } else {
                connectionShutdown = true;
            }
            conditionalVar.notify_one();
        };

    auto onConnectionShutdown = [&](Aws::Crt::Http::HttpClientConnection &, int errorCode) {
        util::TempModule tempModule{getModule()};
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        connectionShutdown = true;
        if(errorCode) {
            errorOccurred = true;
        }
        conditionalVar.notify_one();
    };

    Aws::Crt::Http::HttpClientConnectionOptions httpClientConnectionOptions;
    httpClientConnectionOptions.Bootstrap = &clientBootstrap;
    httpClientConnectionOptions.OnConnectionSetupCallback = onConnectionSetup;
    httpClientConnectionOptions.OnConnectionShutdownCallback = onConnectionShutdown;
    httpClientConnectionOptions.SocketOptions = socketOptions;
    httpClientConnectionOptions.TlsOptions = tlsConnectionOptions;
    httpClientConnectionOptions.HostName =
        std::string((const char *) hostName.ptr, hostName.len);
    httpClientConnectionOptions.Port = PORT_NUM;

    std::unique_lock<std::mutex> semaphoreULock(semaphoreLock);
    if(!Aws::Crt::Http::HttpClientConnection::CreateConnection(
           httpClientConnectionOptions, allocator)) {
        LOG.atError().log("Failed to create connection");
        throw std::runtime_error("Failed to create connection");
    }
    conditionalVar.wait(semaphoreULock, [&]() { return connection || connectionShutdown; });

    // TODO:: Find something better than throwing error at this state
    if(errorOccurred || connectionShutdown || !connection) {
        LOG.atError().log("Failed to establish successful connection");
        throw std::runtime_error("Failed to establish successful connection");
    }

    LOG.atInfo().kv("Creating log group with name", logGroupName).log();

    auto logGroupRequest = Aws::Crt::MakeShared<Aws::Crt::Http::HttpRequest>(allocator);
    logGroupRequest->SetMethod(Aws::Crt::ByteCursorFromCString("POST"));
    logGroupRequest->SetPath(uri.GetPath());

    Aws::Crt::Http::HttpRequestOptions requestOptions;

    int logGroupResponseCode = 0;
    requestOptions.request = logGroupRequest.get();

    bool streamCompleted = false;
    requestOptions.onStreamComplete = [&](Aws::Crt::Http::HttpStream &, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        streamCompleted = true;
        if(errorCode) {
            errorOccurred = true;
        }
        conditionalVar.notify_one();
    };
    requestOptions.onIncomingHeadersBlockDone = nullptr;
    requestOptions.onIncomingHeaders = [&](Aws::Crt::Http::HttpStream &stream,
                                           enum aws_http_header_block,
                                           const Aws::Crt::Http::HttpHeader *,
                                           std::size_t) {
        logGroupResponseCode = stream.GetResponseStatusCode();
    };
    std::stringstream receivedBody;
    requestOptions.onIncomingBody = [&](Aws::Crt::Http::HttpStream &,
                                        const Aws::Crt::ByteCursor &data) {
        receivedBody.write((const char *) data.ptr, data.len);
    };

    Aws::Crt::Http::HttpHeader contentHeader;
    contentHeader.name = Aws::Crt::ByteCursorFromCString("Content-Type");
    contentHeader.value = Aws::Crt::ByteCursorFromCString("application/x-amz-json-1.1");

    Aws::Crt::Http::HttpHeader hostHeader;
    hostHeader.name = Aws::Crt::ByteCursorFromCString("host");
    hostHeader.value = hostName;

    Aws::Crt::Http::HttpHeader actionHeader;
    actionHeader.name = Aws::Crt::ByteCursorFromCString("X-Amz-Target");
    actionHeader.value = Aws::Crt::ByteCursorFromCString("Logs_20140328.CreateLogGroup");

    logGroupRequest->AddHeader(contentHeader);
    logGroupRequest->AddHeader(hostHeader);
    logGroupRequest->AddHeader(actionHeader);

    rapidjson::Document createLogGroupBody;
    createLogGroupBody.SetObject();
    createLogGroupBody.AddMember("logGroupName", logGroupName,
                                 createLogGroupBody.GetAllocator());
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    createLogGroupBody.Accept(writer);
    const char* logGroupRequestBodyStr = buffer.GetString();
    std::shared_ptr<Aws::Crt::Io::IStream> createLogGroupBodyStream =
        std::make_shared<std::istringstream>(logGroupRequestBodyStr);

    uint64_t dataLen = strlen(logGroupRequestBodyStr);
    if (dataLen > 0)
    {
        std::string contentLength = std::to_string(dataLen);
        Aws::Crt::Http::HttpHeader contentLengthHeader;
        contentLengthHeader.name = Aws::Crt::ByteCursorFromCString("content-length");
        contentLengthHeader.value = Aws::Crt::ByteCursorFromCString(contentLength.c_str());
        logGroupRequest->AddHeader(contentLengthHeader);
        logGroupRequest->SetBody(createLogGroupBodyStream);
    }

    // Sign the request
    LOG.atInfo().log("Signing log group request");
    SignWaiter waiter;
    signer->SignRequest(
            logGroupRequest, signingConfig, [&](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request, int errorCode) {
                waiter.OnSigningComplete(request, errorCode);
            });
    waiter.Wait();

    auto stream = connection->NewClientStream(requestOptions);
    if(!stream->Activate()) {
        LOG.atError().log("Failed to activate stream and create log group");
        throw std::runtime_error("Failed to activate stream and create log group");
    }

    conditionalVar.wait(semaphoreULock, [&]() { return streamCompleted; });

    connection->Close();
    conditionalVar.wait(semaphoreULock, [&]() { return connectionShutdown; });

    LOG.atInfo().event("CreateLogGroup Status").kv("response_code",
                                                   logGroupResponseCode).log();

    LOG.atInfo().log("Response body from HTTP request: " + receivedBody.str());
}

void LogManager::setLogStream(const std::string& logStreamName, const std::string& logGroupName) {
    LOG.atDebug().log("Begin create log stream logic");
    auto allocator = Aws::Crt::DefaultAllocator();
    std::string uriAsString = "https://logs." + _logGroup.region + ".amazonaws.com/";
    aws_io_library_init(allocator);

    // Create Credentials to pass to SigV4 signer
    auto responseCred = _credentials.get<std::string>("Response");
    auto buffTest = ggapi::Buffer::create().put(0, std::string_view{responseCred}).fromJson();
    auto buffTestAsStruct = ggapi::Struct{buffTest};
    auto accessKey = buffTestAsStruct.get<std::string>("AccessKeyId");
    auto secretAccessKey = buffTestAsStruct.get<std::string>("SecretAccessKey");
    auto token = buffTestAsStruct.get<std::string>("Token");
    auto expiration = buffTestAsStruct.get<std::string>("Expiration");

    // TODO: Convert TES expiration to uint_64 and include it, verify if needed
    auto credentialsForRequest = Aws::Crt::MakeShared<Aws::Crt::Auth::Credentials>(
            allocator,
            aws_byte_cursor_from_c_str(accessKey.c_str()),
            aws_byte_cursor_from_c_str(secretAccessKey.c_str()),
            aws_byte_cursor_from_c_str(token.c_str()),
            UINT64_MAX
    );
    std::cout << ":::::::::::::::::::::::::" << std::endl;
    printf(PRInSTR "\n", aws_byte_cursor_from_c_str(accessKey.c_str()));
    std::cout << ":::::::::::::::::::::::::" << std::endl;
    printf(PRInSTR "\n", aws_byte_cursor_from_c_str(secretAccessKey.c_str()));
    std::cout << ":::::::::::::::::::::::::" << std::endl;
    // SigV4 Signer
    auto signer = Aws::Crt::MakeShared<Aws::Crt::Auth::Sigv4HttpRequestSigner>(allocator, allocator);
    Aws::Crt::Auth::AwsSigningConfig signingConfig(allocator);
    signingConfig.SetRegion(_logGroup.region.c_str());
    signingConfig.SetSigningAlgorithm(Aws::Crt::Auth::SigningAlgorithm::SigV4);
    signingConfig.SetSignatureType(Aws::Crt::Auth::SignatureType::HttpRequestViaHeaders);
    signingConfig.SetService("logs");
    signingConfig.SetSigningTimepoint(Aws::Crt::DateTime::Now());
    // signingConfig.SetSignedBodyValue(Aws::Crt::Auth::SignedBodyValue::UnsignedPayloadStr());
    // signingConfig.SetUseDoubleUriEncode(false);
    // signingConfig.SetShouldNormalizeUriPath(true);
    // signingConfig.SetSignedBodyHeader(Aws::Crt::Auth::SignedBodyHeaderType::XAmzContentSha256);
    signingConfig.SetCredentials(credentialsForRequest);

    // Setup Connection TLS
    LOG.atInfo().log("starting tls connection setup");

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions =
            Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
    Aws::Crt::Io::TlsContext tlsContext(
            tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    if (tlsContext.GetInitializationError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create TLS context");
        throw std::runtime_error("Failed to create TLS context");
    }
    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    Aws::Crt::ByteCursor urlCursor = Aws::Crt::ByteCursorFromCString(uriAsString.c_str());
    Aws::Crt::Io::Uri uri(urlCursor, allocator);

    auto hostName = uri.GetHostName();
    tlsConnectionOptions.SetServerName(hostName);

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(TIME_OUT_MS);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    if (eventLoopGroup.LastError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create event loop group");
        throw std::runtime_error("Failed to create event loop group");
    }
    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    if (defaultHostResolver.LastError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create default host resolver");
        throw std::runtime_error("Failed to create default host resolver");
    }
    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    if (clientBootstrap.LastError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create client bootstrap");
        throw std::runtime_error("Failed to create client bootstrap");
    }
    clientBootstrap.EnableBlockingShutdown();

    std::shared_ptr<Aws::Crt::Http::HttpClientConnection> connection(nullptr);
    bool errorOccurred = true;
    bool connectionShutdown = false;

    //TODO: Check reuse of variables
    std::condition_variable conditionalVar;
    std::mutex semaphoreLock;

    auto onConnectionSetup =
        [&](const std::shared_ptr<Aws::Crt::Http::HttpClientConnection> &newConnection,
            int errorCode) {
            util::TempModule tempModule{getModule()};
            std::lock_guard<std::mutex> lockGuard(semaphoreLock);
            if (!errorCode) {
                LOG.atInfo().log("Successful on establishing connection.");
                connection = newConnection;
                errorOccurred = false;
            } else {
                connectionShutdown = true;
            }
            conditionalVar.notify_one();
        };

    auto onConnectionShutdown = [&](Aws::Crt::Http::HttpClientConnection &, int errorCode) {
        util::TempModule tempModule{getModule()};
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        connectionShutdown = true;
        if (errorCode) {
            errorOccurred = true;
        }
        conditionalVar.notify_one();
    };

    Aws::Crt::Http::HttpClientConnectionOptions httpClientConnectionOptions;
    httpClientConnectionOptions.Bootstrap = &clientBootstrap;
    httpClientConnectionOptions.OnConnectionSetupCallback = onConnectionSetup;
    httpClientConnectionOptions.OnConnectionShutdownCallback = onConnectionShutdown;
    httpClientConnectionOptions.SocketOptions = socketOptions;
    httpClientConnectionOptions.TlsOptions = tlsConnectionOptions;
    httpClientConnectionOptions.HostName =
            std::string((const char *) hostName.ptr, hostName.len);
    httpClientConnectionOptions.Port = PORT_NUM;

    std::unique_lock<std::mutex> semaphoreULock(semaphoreLock);
    if (!Aws::Crt::Http::HttpClientConnection::CreateConnection(
            httpClientConnectionOptions, allocator)) {
        LOG.atError().log("Failed to create connection");
        throw std::runtime_error("Failed to create connection");
    }
    conditionalVar.wait(semaphoreULock, [&]() { return connection || connectionShutdown; });

    // TODO:: Find something better than throwing error at this state
    if (errorOccurred || connectionShutdown || !connection) {
        LOG.atError().log("Failed to establish successful connection");
        throw std::runtime_error("Failed to establish successful connection");
    }

    // Declaring headers shared by each http request
    Aws::Crt::Http::HttpHeader versionHeader;
    versionHeader.name = Aws::Crt::ByteCursorFromCString("Version");
    versionHeader.value = Aws::Crt::ByteCursorFromCString("2014-03-28");

    Aws::Crt::Http::HttpHeader contentHeader;
    contentHeader.name = Aws::Crt::ByteCursorFromCString("Content-Type");
    contentHeader.value = Aws::Crt::ByteCursorFromCString("application/x-amz-json-1.1");

    Aws::Crt::Http::HttpHeader connectionHeader;
    connectionHeader.name = Aws::Crt::ByteCursorFromCString("Connection");
    connectionHeader.value = Aws::Crt::ByteCursorFromCString("Keep-Alive");

    Aws::Crt::Http::HttpHeader hostHeader;
    hostHeader.name = Aws::Crt::ByteCursorFromCString("host");
    hostHeader.value = hostName;

    LOG.atInfo().kv("creating log stream", logStreamName).log();

    auto logGroupRequest = Aws::Crt::MakeShared<Aws::Crt::Http::HttpRequest>(allocator);
    logGroupRequest->SetMethod(Aws::Crt::ByteCursorFromCString("POST"));
    //TODO: verify - not setting anything for the path and will reuse the uri for each request
    logGroupRequest->SetPath(uri.GetPath());

    Aws::Crt::Http::HttpRequestOptions requestOptions;

    int logGroupResponseCode = 0;
    requestOptions.request = logGroupRequest.get();

    bool streamCompleted = false;
    requestOptions.onStreamComplete = [&](Aws::Crt::Http::HttpStream &, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        streamCompleted = true;
        if (errorCode) {
            errorOccurred = true;
        }
        conditionalVar.notify_one();
    };
    requestOptions.onIncomingHeadersBlockDone = nullptr;
    requestOptions.onIncomingHeaders = [&](Aws::Crt::Http::HttpStream &stream,
                                           enum aws_http_header_block,
                                           const Aws::Crt::Http::HttpHeader *,
                                           std::size_t) {
        logGroupResponseCode = stream.GetResponseStatusCode();
    };
    std::stringstream receivedBody;
    requestOptions.onIncomingBody = [&](Aws::Crt::Http::HttpStream &,
                                        const Aws::Crt::ByteCursor &data) {
        receivedBody.write((const char *) data.ptr, data.len);
    };


//        Aws::Crt::Http::HttpHeader actionHeader;
//        actionHeader.name = Aws::Crt::ByteCursorFromCString("Action");
//        actionHeader.value = Aws::Crt::ByteCursorFromCString("CreateLogGroup");
//        logGroupRequest->AddHeader(actionHeader);

    //TODO: try this if action above doesn't work
    Aws::Crt::Http::HttpHeader actionHeader;
    actionHeader.name = Aws::Crt::ByteCursorFromCString("X-Amz-Target");
    actionHeader.value = Aws::Crt::ByteCursorFromCString("Logs_20140328.CreateLogStream");
    logGroupRequest->AddHeader(actionHeader);

    // logGroupRequest->AddHeader(versionHeader);
    logGroupRequest->AddHeader(contentHeader);
    // logGroupRequest->AddHeader(connectionHeader);
    logGroupRequest->AddHeader(hostHeader);

    rapidjson::Document createLogStreamBody;
    createLogStreamBody.SetObject();
    createLogStreamBody.AddMember("logGroupName", logGroupName,
                                  createLogStreamBody.GetAllocator());
    createLogStreamBody.AddMember("logStreamName", logStreamName,
                                  createLogStreamBody.GetAllocator());
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    createLogStreamBody.Accept(writer);
    const char* logStreamRequestBodyStr = buffer.GetString();
    std::shared_ptr<Aws::Crt::Io::IStream> createLogStreamBodyStream =
            std::make_shared<std::istringstream>(logStreamRequestBodyStr);
    // logGroupRequest->SetBody(createLogGroupBodyStream);

//        const char *logGroupRequestBodyStr = "{\"logGroupName\": \"my-test-log-grouc\"}";
//        std::cout << ":::::::::::::::::::::::::"<< std::endl;
//        std::shared_ptr<Aws::Crt::Io::IStream> bodyStream =
//                std::make_shared<std::istringstream>(logGroupRequestBodyStr);

    uint64_t dataLen = strlen(logStreamRequestBodyStr);
    // if (!bodyStream->GetLength(dataLen))
    // {
    //     std::cerr << "failed to get length of input stream.\n";
    //     exit(1);
    // }
    if (dataLen > 0) {
        std::string contentLength = std::to_string(dataLen);
        Aws::Crt::Http::HttpHeader contentLengthHeader;
        contentLengthHeader.name = Aws::Crt::ByteCursorFromCString("content-length");
        contentLengthHeader.value = Aws::Crt::ByteCursorFromCString(contentLength.c_str());
        logGroupRequest->AddHeader(contentLengthHeader);
        logGroupRequest->SetBody(createLogStreamBodyStream);
    }

    LOG.atInfo().log("ASLKFJLKGDLZJGGLFG");
    LOG.atInfo().log(logStreamRequestBodyStr);

    // Sign the request
    LOG.atInfo().log("signing log group request");

    SignWaiter waiter;
    signer->SignRequest(
            logGroupRequest, signingConfig,
            [&](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request, int errorCode) {
                waiter.OnSigningComplete(request, errorCode);
            });
    waiter.Wait();

    auto stream = connection->NewClientStream(requestOptions);
    if (!stream->Activate()) {
        LOG.atError().log("Failed to activate stream and create log group");
        throw std::runtime_error("Failed to activate stream and create log group");
    }

    conditionalVar.wait(semaphoreULock, [&]() { return streamCompleted; });

    connection->Close();
    conditionalVar.wait(semaphoreULock, [&]() { return connectionShutdown; });

    LOG.atInfo().event("CreateLogStream Status").kv("response_code",
                                                   logGroupResponseCode).log();

    LOG.atInfo().log("Response body from HTTP request: " + receivedBody.str());
}

void LogManager::uploadLogs(const rapidjson::Document& putLogEventsRequestBody) {
    LOG.atInfo().log("starting client setup");
    auto allocator = Aws::Crt::DefaultAllocator();
    std::string uriAsString = "https://logs." + _logGroup.region + ".amazonaws.com/";
    LOG.atInfo().log("URI set as: " + uriAsString);
    aws_io_library_init(allocator);

    // Create Credentials to pass to SigV4 signer
    auto responseCred = _credentials.get<std::string>("Response");
    auto buffTest = ggapi::Buffer::create().put(0, std::string_view{responseCred}).fromJson();
    auto buffTestAsStruct = ggapi::Struct{buffTest};
    auto accessKey = buffTestAsStruct.get<std::string>("AccessKeyId");
    auto secretAccessKey = buffTestAsStruct.get<std::string>("SecretAccessKey");
    auto token = buffTestAsStruct.get<std::string>("Token");
    auto expiration = buffTestAsStruct.get<std::string>("Expiration");

    LOG.atInfo().log("ACCESS KEY BELOW:");
    LOG.atInfo().log(accessKey);
    LOG.atInfo().log("SECRET ACCESS KEY BELOW:");
    LOG.atInfo().log(secretAccessKey);
    LOG.atInfo().log("TOKEN BELOW:");
    LOG.atInfo().log(token);
    LOG.atInfo().log("EXPIRATION BELOW:");
    LOG.atInfo().log(expiration);
//
//    accessKey = "ASIAZECTMMURTHSCJY5V";
//    secretAccessKey = "TZcqqf9kAYb7vjsR71UKgGXOJ1I3tjMa9dfHZzLr";
//    token = "IQoJb3JpZ2luX2VjEC0aCXVzLWVhc3QtMSJHMEUCIE+SyAUiHfvKauohd1EQHmVybmjPPA4Sl+R3RNPUZU67AiEAz5wEypdBxO9jwi8TrWRwNCSVQ0abb7bwo/c0bAR9SjUqngIIZRACGgw2MjcyNDAxMDExNTUiDFJMMIHjI3vwdE3NJyr7ASoIA63/8iM3m+E4THNsYDsgDIbwu/jWNi/D16+QGQ/IsMTro5H/nqyLXEnjBrCokNM9UboeYnschg+tcrAAyyBjqK6Ix9RtKHO6KbzEUg94a3CZk/ThZLkTyvyJrrkufLvQkFliQej32tnJakTNjLQ0eKNtVTxWvPDvneqwNl0MkKwgQVy/oeqaihXmBUmMJ7TKwiEeWq6C5MUPlxBATVtWfaJdGKB4N7+U8lbUA8r8AxT3JRKpgzA7gskiY0tSMkMQ+qH8xVRc1oMpFnWdQhnw4j9YKrqQsgsVQei1yNga3a1nwEBiRgTcK3adSuV1a/ZFw0m4pZPPMpUsMOiu5rAGOp0Bl46QgzWz9cMGjU2khz4WNKZWSEJyYle2anK1NGRvSQ6Npgk6OVc/S+nqb0UVEL64t7poiQ+V7m1kgM6CUt3QuN87YHiSQwcMfNmx+N8vVFh0o7t/WH8qmnlS3ehzpsqBKzOkYnq0JAM43FmQLE/ezmlovbsbwNajdThVIwIHwidd2l+skBrBWn2yQ66POHsmj3/o5eFfVD7a3SFvXw==";

    // TODO: Convert TES expiration to uint_64 and include it, verify if needed
    auto credentialsForRequest = Aws::Crt::MakeShared<Aws::Crt::Auth::Credentials>(
            allocator,
            aws_byte_cursor_from_c_str(accessKey.c_str()),
            aws_byte_cursor_from_c_str(secretAccessKey.c_str()),
            aws_byte_cursor_from_c_str(token.c_str()),
            UINT64_MAX
    );
    std::cout << ":::::::::::::::::::::::::" << std::endl;
    printf(PRInSTR "\n", aws_byte_cursor_from_c_str(accessKey.c_str()));
    std::cout << ":::::::::::::::::::::::::" << std::endl;
    printf(PRInSTR "\n", aws_byte_cursor_from_c_str(secretAccessKey.c_str()));
    std::cout << ":::::::::::::::::::::::::" << std::endl;
    // SigV4 Signer
    auto signer = Aws::Crt::MakeShared<Aws::Crt::Auth::Sigv4HttpRequestSigner>(allocator, allocator);
    Aws::Crt::Auth::AwsSigningConfig signingConfig(allocator);
    signingConfig.SetRegion(_logGroup.region.c_str());
    signingConfig.SetSigningAlgorithm(Aws::Crt::Auth::SigningAlgorithm::SigV4);
    signingConfig.SetSignatureType(Aws::Crt::Auth::SignatureType::HttpRequestViaHeaders);
    signingConfig.SetService("logs");
    signingConfig.SetSigningTimepoint(Aws::Crt::DateTime::Now());
    // signingConfig.SetSignedBodyValue(Aws::Crt::Auth::SignedBodyValue::UnsignedPayloadStr());
    // signingConfig.SetUseDoubleUriEncode(false);
    // signingConfig.SetShouldNormalizeUriPath(true);
    // signingConfig.SetSignedBodyHeader(Aws::Crt::Auth::SignedBodyHeaderType::XAmzContentSha256);
    signingConfig.SetCredentials(credentialsForRequest);

    // Setup Connection TLS
    LOG.atInfo().log("starting tls connection setup");

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions =
            Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
    Aws::Crt::Io::TlsContext tlsContext(
            tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    if (tlsContext.GetInitializationError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create TLS context");
        throw std::runtime_error("Failed to create TLS context");
    }
    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    Aws::Crt::ByteCursor urlCursor = Aws::Crt::ByteCursorFromCString(uriAsString.c_str());
    Aws::Crt::Io::Uri uri(urlCursor, allocator);

    auto hostName = uri.GetHostName();
    tlsConnectionOptions.SetServerName(hostName);

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(TIME_OUT_MS);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    if (eventLoopGroup.LastError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create event loop group");
        throw std::runtime_error("Failed to create event loop group");
    }
    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    if (defaultHostResolver.LastError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create default host resolver");
        throw std::runtime_error("Failed to create default host resolver");
    }
    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    if (clientBootstrap.LastError() != AWS_ERROR_SUCCESS) {
        LOG.atError().log("Failed to create client bootstrap");
        throw std::runtime_error("Failed to create client bootstrap");
    }
    clientBootstrap.EnableBlockingShutdown();

    std::shared_ptr<Aws::Crt::Http::HttpClientConnection> connection(nullptr);
    bool errorOccurred = true;
    bool connectionShutdown = false;

    //TODO: Check reuse of variables
    std::condition_variable conditionalVar;
    std::mutex semaphoreLock;

    auto onConnectionSetup =
            [&](const std::shared_ptr<Aws::Crt::Http::HttpClientConnection> &newConnection,
                int errorCode) {
                util::TempModule tempModule{getModule()};
                std::lock_guard<std::mutex> lockGuard(semaphoreLock);
                if (!errorCode) {
                    LOG.atInfo().log("Successful on establishing connection.");
                    connection = newConnection;
                    errorOccurred = false;
                } else {
                    connectionShutdown = true;
                }
                conditionalVar.notify_one();
            };

    auto onConnectionShutdown = [&](Aws::Crt::Http::HttpClientConnection &, int errorCode) {
        util::TempModule tempModule{getModule()};
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        connectionShutdown = true;
        if (errorCode) {
            errorOccurred = true;
        }
        conditionalVar.notify_one();
    };

    Aws::Crt::Http::HttpClientConnectionOptions httpClientConnectionOptions;
    httpClientConnectionOptions.Bootstrap = &clientBootstrap;
    httpClientConnectionOptions.OnConnectionSetupCallback = onConnectionSetup;
    httpClientConnectionOptions.OnConnectionShutdownCallback = onConnectionShutdown;
    httpClientConnectionOptions.SocketOptions = socketOptions;
    httpClientConnectionOptions.TlsOptions = tlsConnectionOptions;
    httpClientConnectionOptions.HostName =
            std::string((const char *) hostName.ptr, hostName.len);
    httpClientConnectionOptions.Port = PORT_NUM;

    std::unique_lock<std::mutex> semaphoreULock(semaphoreLock);
    if (!Aws::Crt::Http::HttpClientConnection::CreateConnection(
            httpClientConnectionOptions, allocator)) {
        LOG.atError().log("Failed to create connection");
        throw std::runtime_error("Failed to create connection");
    }
    conditionalVar.wait(semaphoreULock, [&]() { return connection || connectionShutdown; });

    // TODO:: Find something better than throwing error at this state
    if (errorOccurred || connectionShutdown || !connection) {
        LOG.atError().log("Failed to establish successful connection");
        throw std::runtime_error("Failed to establish successful connection");
    }

    // Declaring headers shared by each http request
    Aws::Crt::Http::HttpHeader versionHeader;
    versionHeader.name = Aws::Crt::ByteCursorFromCString("Version");
    versionHeader.value = Aws::Crt::ByteCursorFromCString("2014-03-28");

    Aws::Crt::Http::HttpHeader contentHeader;
    contentHeader.name = Aws::Crt::ByteCursorFromCString("Content-Type");
    contentHeader.value = Aws::Crt::ByteCursorFromCString("application/x-amz-json-1.1");

    Aws::Crt::Http::HttpHeader connectionHeader;
    connectionHeader.name = Aws::Crt::ByteCursorFromCString("Connection");
    connectionHeader.value = Aws::Crt::ByteCursorFromCString("Keep-Alive");

    Aws::Crt::Http::HttpHeader hostHeader;
    hostHeader.name = Aws::Crt::ByteCursorFromCString("host");
    hostHeader.value = hostName;

    LOG.atInfo().log("putting events");

        auto logGroupRequest = Aws::Crt::MakeShared<Aws::Crt::Http::HttpRequest>(allocator);
        logGroupRequest->SetMethod(Aws::Crt::ByteCursorFromCString("POST"));
        //TODO: verify - not setting anything for the path and will reuse the uri for each request
        logGroupRequest->SetPath(uri.GetPath());

        Aws::Crt::Http::HttpRequestOptions requestOptions;

        int logGroupResponseCode = 0;
        requestOptions.request = logGroupRequest.get();

        bool streamCompleted = false;
        requestOptions.onStreamComplete = [&](Aws::Crt::Http::HttpStream &, int errorCode) {
            std::lock_guard<std::mutex> lockGuard(semaphoreLock);
            streamCompleted = true;
            if (errorCode) {
                errorOccurred = true;
            }
            conditionalVar.notify_one();
        };
        requestOptions.onIncomingHeadersBlockDone = nullptr;
        requestOptions.onIncomingHeaders = [&](Aws::Crt::Http::HttpStream &stream,
                                               enum aws_http_header_block,
                                               const Aws::Crt::Http::HttpHeader *,
                                               std::size_t) {
            logGroupResponseCode = stream.GetResponseStatusCode();
        };
        std::stringstream receivedBody;
        requestOptions.onIncomingBody = [&](Aws::Crt::Http::HttpStream &,
                                            const Aws::Crt::ByteCursor &data) {
            receivedBody.write((const char *) data.ptr, data.len);
        };


//        Aws::Crt::Http::HttpHeader actionHeader;
//        actionHeader.name = Aws::Crt::ByteCursorFromCString("Action");
//        actionHeader.value = Aws::Crt::ByteCursorFromCString("CreateLogGroup");
//        logGroupRequest->AddHeader(actionHeader);

        //TODO: try this if action above doesn't work
        Aws::Crt::Http::HttpHeader actionHeader;
        actionHeader.name = Aws::Crt::ByteCursorFromCString("X-Amz-Target");
        actionHeader.value = Aws::Crt::ByteCursorFromCString("Logs_20140328.PutLogEvents");
        logGroupRequest->AddHeader(actionHeader);

        // logGroupRequest->AddHeader(versionHeader);
        logGroupRequest->AddHeader(contentHeader);
        // logGroupRequest->AddHeader(connectionHeader);
        logGroupRequest->AddHeader(hostHeader);

        LOG.atInfo().log("AAAAAAAAAAAttempting to convert body to a string");

    rapidjson::StringBuffer buffer;
    LOG.atInfo().log("A.5");
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    LOG.atInfo().log("BBBBBBBBBBBBBBBBBBBBBBB");
        putLogEventsRequestBody.Accept(writer);
        LOG.atInfo().log("CCCCCCCCCCCCCCCCCC");
        const char* putLogEventsRequestBodyStr = buffer.GetString();
    LOG.atInfo().log("DDDDDDDDDDDDDDDDDDDDDE");
        std::shared_ptr<Aws::Crt::Io::IStream> putLogEventsBodyStream =
                std::make_shared<std::istringstream>(putLogEventsRequestBodyStr);
    LOG.atInfo().log("EEEEEEEEEEEEEEEE");

//        const char *logGroupRequestBodyStr = "{\"logGroupName\": \"my-test-log-grouc\"}";
//        std::cout << ":::::::::::::::::::::::::"<< std::endl;
//        std::shared_ptr<Aws::Crt::Io::IStream> bodyStream =
//                std::make_shared<std::istringstream>(logGroupRequestBodyStr);

        uint64_t dataLen = strlen(putLogEventsRequestBodyStr);
        // if (!bodyStream->GetLength(dataLen))
        // {
        //     std::cerr << "failed to get length of input stream.\n";
        //     exit(1);
        // }
        if (dataLen > 0) {
            std::string contentLength = std::to_string(dataLen);
            Aws::Crt::Http::HttpHeader contentLengthHeader;
            contentLengthHeader.name = Aws::Crt::ByteCursorFromCString("content-length");
            contentLengthHeader.value = Aws::Crt::ByteCursorFromCString(contentLength.c_str());
            logGroupRequest->AddHeader(contentLengthHeader);
            logGroupRequest->SetBody(putLogEventsBodyStream);
        }

        LOG.atInfo().log("ASLKFJLKGDLZJGGLFG");
        LOG.atInfo().log(putLogEventsRequestBodyStr);

        // Sign the request
        LOG.atInfo().log("signing log group request");

        SignWaiter waiter;
        signer->SignRequest(
                logGroupRequest, signingConfig,
                [&](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request, int errorCode) {
                    waiter.OnSigningComplete(request, errorCode);
                });
        waiter.Wait();

        auto stream = connection->NewClientStream(requestOptions);
        if (!stream->Activate()) {
            LOG.atError().log("Failed to activate stream and create log group");
            throw std::runtime_error("Failed to activate stream and create log group");
        }

        conditionalVar.wait(semaphoreULock, [&]() { return streamCompleted; });

        connection->Close();
        conditionalVar.wait(semaphoreULock, [&]() { return connectionShutdown; });

        LOG.atInfo().event("PutLogEvents Status").kv("response_code",
                                                       logGroupResponseCode).log();

        LOG.atInfo().log("Response body from HTTP request: " + receivedBody.str());
}

void LogManager::processLogsAndUpload() {
    std::cout << "[Log_Manager] Start Logic " << std::endl;
    // TODO: remove hardcoded values
    auto system = _system;
    auto nucleus = _nucleus;
    _logGroup.region = nucleus.getValue<std::string>({"configuration", "awsRegion"});
    _logGroup.componentType = "GreengrassSystemComponent";
    _logGroup.componentName = "System";
    _logStream.thingName = system.getValue<Aws::Crt::String>({THING_NAME});
    std::time_t currentTime = std::time(nullptr);
    std::stringstream timeStringStream;
    timeStringStream << currentTime;
    _logStream.date = timeStringStream.str();
    LOG.atInfo().kv("Timestamp for log stream", _logStream.date).log();
    auto logFilePath = system.getValue<std::string>({"rootpath"})
                       + "/logs/greengrass.log";

    std::string logGroupName =
        "/aws/greengrass/" + _logGroup.componentType + "/" + _logGroup.region + "/" +
        _logGroup.componentName;
    // std::string logGroupName = "gglitelogmanager";
    std::string logStreamName = "/" + _logStream.date + "/thing/" +
                                std::string(_logStream.thingName.c_str());

    LOG.atInfo().log("Log group name: " + logGroupName);
    LOG.atInfo().log("Log stream name: " + logStreamName);

    // read log file and build request body
    std::ifstream file(logFilePath);
    if (!file.is_open()) {
        LOG.atInfo().event("Unable to open Greengrass log file").log();
        return;
    }

    std::string logLine;
    rapidjson::Document logEvents;
    logEvents.SetArray();
    // TODO: chunk requests
    while (getline(file, logLine)) {
        // reading greengrass.log line by line here, process each line as needed
        rapidjson::Document readLog;
        readLog.SetObject();
        rapidjson::Value inputLogEvent;
        std::ignore = readLog.Parse(logLine.c_str());
        inputLogEvent.SetObject();
        inputLogEvent.AddMember("timestamp", readLog["timestamp"],
                                logEvents.GetAllocator());
        inputLogEvent.AddMember("message", logLine,
                                logEvents.GetAllocator());
        logEvents.PushBack(inputLogEvent, logEvents.GetAllocator());
    }

    rapidjson::Document body;
    body.SetObject();
    body.AddMember("logStreamName", logStreamName, body.GetAllocator());
    body.AddMember("logGroupName", logGroupName, body.GetAllocator());
    body.AddMember("logEvents", logEvents, body.GetAllocator());

    // Callback on success request stream response
    //TODO: verify - response body for put log events call contains "next sequence token" and "rejected log events", token not needed, currently not checking for rejected logs
//    std::stringstream downloadContent;
//    Aws::Crt::Http::HttpRequestOptions requestOptions;
//    requestOptions.onIncomingBody = [&](Aws::Crt::Http::HttpStream &,
//                                        const Aws::Crt::ByteCursor &data) {
//        downloadContent.write((const char *) data.ptr, data.len);
//    };

    LogManager::createLogGroup(logGroupName);

    LogManager::setLogStream(logStreamName, logGroupName);

    LogManager::uploadLogs(body);
}
