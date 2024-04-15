#define RAPIDJSON_HAS_STDSTRING 1

#include "log_manager.hpp"
#include "futures.hpp"
#include <fstream>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <temp_module.hpp>
#include <aws/crt/auth/Credentials.h>

const auto LOG = ggapi::Logger::of("LogManager");
bool logGroupCreated = false;

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
    // TODO: retrieve and process system config
    std::unique_lock guard{_mutex};
    _nucleus = data.getValue<ggapi::Struct>({"nucleus"});
    _system = data.getValue<ggapi::Struct>({"system"});
    LOG.atInfo().log("initializing log manager");
    return true;
}

bool LogManager::onStart(ggapi::Struct data) {
    retrieveCredentialsFromTES();
    if (_credentials.hasKey("Response")) {
        LOG.atInfo().log("retrieved credentials successfully, starting the loop");
        while(true) {
            LOG.atInfo().log("preparing to process logs");
            LogManager::processLogsAndUpload();
            ggapi::sleep(300);
        }
    } else {
        LOG.atError().log("Could not retrieve credentials from TES");
        return false;
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
    LOG.atInfo().log("calling topic to request credentials");
    auto tesFuture = ggapi::Subscription::callTopicFirst(
        ggapi::Symbol{TES_REQUEST_TOPIC}, request);
    if(tesFuture) {
        _credentials = ggapi::Struct(tesFuture.waitAndGetValue());
    }
    else {
        _credentials = {};
    }
    // _credentials.toJson().write(std::cout);
}

void LogManager::setupClient(const rapidjson::Document& putLogEventsRequestBody,
                             const std::string logGroupName, const std::string logStreamName) {
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

    // printf(PRInSTR "\n", AWS_BYTE_BUF_PRI(aws_byte_cursor_from_c_str(accessKey.c_str()));

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

    // Create log group if it doesn't exist
    if (!logGroupCreated) {
        LOG.atInfo().kv("creating log group", logGroupName).log();

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


//        Aws::Crt::Http::HttpHeader actionHeader;
//        actionHeader.name = Aws::Crt::ByteCursorFromCString("Action");
//        actionHeader.value = Aws::Crt::ByteCursorFromCString("CreateLogGroup");
//        logGroupRequest->AddHeader(actionHeader);

        //TODO: try this if action above doesn't work
        Aws::Crt::Http::HttpHeader actionHeader;
        actionHeader.name = Aws::Crt::ByteCursorFromCString("X-Amz-Target");
        actionHeader.value = Aws::Crt::ByteCursorFromCString("Logs_20140328.CreateLogGroup");
        logGroupRequest->AddHeader(actionHeader);

        // logGroupRequest->AddHeader(versionHeader);
        logGroupRequest->AddHeader(contentHeader);
        // logGroupRequest->AddHeader(connectionHeader);
        logGroupRequest->AddHeader(hostHeader);

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
        logGroupRequest->SetBody(createLogGroupBodyStream);

        LOG.atInfo().log("ASLKFJLKGDLZJGGLFG");
        LOG.atInfo().log(logGroupRequestBodyStr);

        // Sign the request
        LOG.atInfo().log("signing log group request");

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
        // TODO: check if log group exists instead of this
        logGroupCreated = true;

        LOG.atInfo().log("Response body from HTTP request: " + receivedBody.str());
    }

    // Setup createLogStream request
    LOG.atInfo().kv("creating log stream request", logStreamName).log();

    auto logStreamRequest = Aws::Crt::MakeShared<Aws::Crt::Http::HttpRequest>(allocator);
    logStreamRequest->SetMethod(Aws::Crt::ByteCursorFromCString("POST"));
    //TODO: verify - not setting anything for the path and reusing the uri
    logStreamRequest->SetPath(uri.GetPathAndQuery());

    Aws::Crt::Http::HttpRequestOptions logStreamRequestOptions;

    int streamResponseCode = 0;
    logStreamRequestOptions.request = logStreamRequest.get();

    bool logStreamRequestCompleted = false;
    logStreamRequestOptions.onStreamComplete = [&](Aws::Crt::Http::HttpStream &, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        logStreamRequestCompleted = true;
        if(errorCode) {
            errorOccurred = true;
        }
        conditionalVar.notify_one();
    };
    logStreamRequestOptions.onIncomingHeadersBlockDone = nullptr;
    logStreamRequestOptions.onIncomingHeaders = [&](Aws::Crt::Http::HttpStream &stream,
                                           enum aws_http_header_block,
                                           const Aws::Crt::Http::HttpHeader *,
                                           std::size_t) {
        streamResponseCode = stream.GetResponseStatusCode();
    };

    Aws::Crt::Http::HttpHeader createLogStreamActionHeader;
    createLogStreamActionHeader.name = Aws::Crt::ByteCursorFromCString("Action");
    createLogStreamActionHeader.value = Aws::Crt::ByteCursorFromCString("CreateLogStream");
    logStreamRequest->AddHeader(createLogStreamActionHeader);

    logStreamRequest->AddHeader(versionHeader);
    logStreamRequest->AddHeader(contentHeader);
    logStreamRequest->AddHeader(connectionHeader);
    logStreamRequest->AddHeader(hostHeader);

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
    logStreamRequest->SetBody(createLogStreamBodyStream);

    // Sign the request
    LOG.atInfo().log("signing log stream request");
    SignWaiter waiter;
    signer->SignRequest(
            logStreamRequest, signingConfig, [&](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request, int errorCode) {
                waiter.OnSigningComplete(request, errorCode);
            });
    waiter.Wait();

    auto createLogStreamStream = connection->NewClientStream(logStreamRequestOptions);
    if(!createLogStreamStream->Activate()) {
        LOG.atError().log("Failed to activate stream and upload logs");
        throw std::runtime_error("Failed to activate stream and upload logs");
    }

    conditionalVar.wait(semaphoreULock, [&]() { return logStreamRequestCompleted; });

    connection->Close();
    conditionalVar.wait(semaphoreULock, [&]() { return connectionShutdown; });

    LOG.atInfo().event("CreateLogStream Status").kv("response_code", streamResponseCode).log();

    // Setup putLogEvent request
    auto putLogsRequest = Aws::Crt::MakeShared<Aws::Crt::Http::HttpRequest>(allocator);
    Aws::Crt::Http::HttpRequestOptions putLogsRequestOptions;

    int putLogsResponseCode = 0;
    putLogsRequestOptions.request = putLogsRequest.get();

    bool putLogsRequestCompleted = false;
    putLogsRequestOptions.onStreamComplete = [&](Aws::Crt::Http::HttpStream &, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        putLogsRequestCompleted = true;
        if(errorCode) {
            errorOccurred = true;
        }
        conditionalVar.notify_one();
    };
    putLogsRequestOptions.onIncomingHeadersBlockDone = nullptr;
    putLogsRequestOptions.onIncomingHeaders = [&](Aws::Crt::Http::HttpStream &stream,
                                           enum aws_http_header_block,
                                           const Aws::Crt::Http::HttpHeader *,
                                           std::size_t) {
        putLogsResponseCode = stream.GetResponseStatusCode();
    };

    putLogsRequest->SetMethod(Aws::Crt::ByteCursorFromCString("POST"));
    putLogsRequest->SetPath(uri.GetPathAndQuery());

    Aws::Crt::Http::HttpHeader actionHeader;
    actionHeader.name = Aws::Crt::ByteCursorFromCString("Action");
    actionHeader.value = Aws::Crt::ByteCursorFromCString("PutLogEvents");
    putLogsRequest->AddHeader(actionHeader);

    //TODO: try this if action above doesn't work
//    Aws::Crt::Http::HttpHeader actionHeader;
//    actionHeader.name = Aws::Crt::ByteCursorFromCString("X-Amz-Target");
//    actionHeader.value = Aws::Crt::ByteCursorFromCString("Logs_20140328.PutLogEvents");
//    request.AddHeader(actionHeader);

    putLogsRequest->AddHeader(versionHeader);
    putLogsRequest->AddHeader(contentHeader);
    putLogsRequest->AddHeader(connectionHeader);
    putLogsRequest->AddHeader(hostHeader);

    //TODO: Look into Date header
//    Aws::Crt::Http::HttpHeader dateHeader;
//    dateHeader.name = Aws::Crt::ByteCursorFromCString("x-amz-date");
//    // need to add value
//    request.AddHeader(dateHeader);

    //TODO: Look into Content-length header
//    Aws::Crt::Http::HttpHeader lengthHeader;
//    lengthHeader.name = Aws::Crt::ByteCursorFromCString("Content-length");
//    const std::string chunkSize = std::to_string(CHUNK_SIZE);
//    lengthHeader.value = Aws::Crt::ByteCursorFromCString(chunkSize.c_str());
//    request.AddHeader(lengthHeader);

    putLogEventsRequestBody.Accept(writer);
    const char* putLogEventsRequestBodyStr = buffer.GetString();
    std::shared_ptr<Aws::Crt::Io::IStream> putLogEventsBodyStream =
            std::make_shared<std::istringstream>(putLogEventsRequestBodyStr);
    putLogsRequest->SetBody(putLogEventsBodyStream);

    // Sign the request
    signer->SignRequest(
            putLogsRequest, signingConfig, [&](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request, int errorCode) {
                waiter.OnSigningComplete(request, errorCode);
            });
    waiter.Wait();

    auto stream = connection->NewClientStream(putLogsRequestOptions);
    if(!stream->Activate()) {
        LOG.atError().log("Failed to activate stream and upload logs");
        throw std::runtime_error("Failed to activate stream and upload logs");
    }

    conditionalVar.wait(semaphoreULock, [&]() { return putLogsRequestCompleted; });

    connection->Close();
    conditionalVar.wait(semaphoreULock, [&]() { return connectionShutdown; });

    LOG.atInfo().event("PutLogEvents Status").kv("response_code", putLogsResponseCode).log();
}

void LogManager::processLogsAndUpload() {
    // TODO: remove hardcoded values
    auto system = _system;
    auto nucleus = _nucleus;
    _logGroup.region = nucleus.getValue<std::string>({"configuration", "awsRegion"});
    _logGroup.componentType = "GreengrassSystemComponent";
    _logGroup.componentName = "System";
    _logStream.thingName = system.getValue<Aws::Crt::String>({THING_NAME});
    // TODO: Actual timestamp
    _logStream.date = "3921313123";
    auto logFilePath = system.getValue<std::string>({"rootpath"})
                       + "/logs/greengrass.log";

//    std::string logGroupName =
//        "/aws/greengrass/" + _logGroup.componentType + "/" + _logGroup.region + "/" +
//        _logGroup.componentName;
    std::string logGroupName = "gglitelogmanager";
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
        rapidjson::Document inputLogEvent;
        std::ignore = readLog.Parse(logLine.c_str());
        inputLogEvent.SetObject();
        inputLogEvent.AddMember("timestamp", readLog["timestamp"],
                                inputLogEvent.GetAllocator());
        inputLogEvent.AddMember("message", logLine,
                                inputLogEvent.GetAllocator());
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

    LogManager::setupClient(body, logGroupName, logStreamName);
}
