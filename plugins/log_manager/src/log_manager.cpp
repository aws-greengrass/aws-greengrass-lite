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

void LogManager::onInitialize(ggapi::Struct data) {
    std::ignore = util::getDeviceSdkApiHandle();
    // TODO: retrieve and process system config
    std::unique_lock guard{_mutex};
    _nucleus = data.getValue<ggapi::Struct>({"nucleus"});
    _system = data.getValue<ggapi::Struct>({"system"});
    LOG.atInfo().log("Initializing log manager");
}

void LogManager::uploadThread(ggapi::Struct data) {
    util::TempModule module(getModule());
    LOG.atInfo().log("Starting upload thread");
    // Give TES some time to boot. Will help most cases.
    // std::this_thread::sleep_for(std::chrono::seconds(5));
    while(true) {
        retrieveCredentialsFromTES();
        if (!_credentials.empty() && _credentials.hasKey("Response")) {
            LOG.atInfo().log("Credentials successfully retrieved from TES");
            LogManager::processLogsAndUpload();
        } else {
            LOG.atError().log("Could not retrieve credentials from TES. Skipping this upload attempt.");
        }
        std::this_thread::sleep_for(std::chrono::seconds(UPLOAD_FREQUENCY));
    }
}

void LogManager::onStart(ggapi::Struct data) {
    LOG.atInfo().log("Beginning persistent logging loop logic");
    _upload = std::thread{&LogManager::uploadThread, this, data};
}

void LogManager::onStop(ggapi::Struct data) {}

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

void LogManager::makeHTTPCallToCloudwatchLogs(const std::string& action, const rapidjson::Document& requestBody) {
    LOG.atInfo().kv("Begin HTTP call to Cloudwatch Logs logic for action", action).log();
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

    auto request = Aws::Crt::MakeShared<Aws::Crt::Http::HttpRequest>(allocator);
    request->SetMethod(Aws::Crt::ByteCursorFromCString("POST"));
    request->SetPath(uri.GetPath());

    Aws::Crt::Http::HttpRequestOptions requestOptions;

    int requestResponseCode = 0;
    requestOptions.request = request.get();

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
        requestResponseCode = stream.GetResponseStatusCode();
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
    actionHeader.value = Aws::Crt::ByteCursorFromCString(action.c_str());

    request->AddHeader(contentHeader);
    request->AddHeader(hostHeader);
    request->AddHeader(actionHeader);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    requestBody.Accept(writer);
    const char* requestBodyStr = buffer.GetString();
    std::shared_ptr<Aws::Crt::Io::IStream> requestBodyStream =
        std::make_shared<std::istringstream>(requestBodyStr);

    LOG.atDebug().kv("Body for outgoing HTTP request", requestBodyStr).log();

    uint64_t dataLen = strlen(requestBodyStr);
    if (dataLen > 0)
    {
        std::string contentLength = std::to_string(dataLen);
        Aws::Crt::Http::HttpHeader contentLengthHeader;
        contentLengthHeader.name = Aws::Crt::ByteCursorFromCString("content-length");
        contentLengthHeader.value = Aws::Crt::ByteCursorFromCString(contentLength.c_str());
        request->AddHeader(contentLengthHeader);
        request->SetBody(requestBodyStream);
    }

    // Sign the request
    LOG.atInfo().log("Signing HTTP request with SigV4");
    SignWaiter waiter;
    signer->SignRequest(
            request, signingConfig, [&](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request, int errorCode) {
                waiter.OnSigningComplete(request, errorCode);
            });
    waiter.Wait();

    auto stream = connection->NewClientStream(requestOptions);
    if(!stream->Activate()) {
        LOG.atError().log("Failed to activate stream for HTTP call");
        throw std::runtime_error("Failed to activate stream for HTTP call");
    }

    conditionalVar.wait(semaphoreULock, [&]() { return streamCompleted; });

    connection->Close();
    conditionalVar.wait(semaphoreULock, [&]() { return connectionShutdown; });

    LOG.atInfo().event("HTTP Response Code").kv("response_code",
                                                requestResponseCode).log();

    LOG.atDebug().log("Response body from HTTP request: " + receivedBody.str());
}

void LogManager::processLogsAndUpload() {
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
    LOG.atInfo().kv("Using timestamp for log stream name", _logStream.date).log();
    auto logFilePath = system.getValue<std::string>({"rootpath"})
                       + "/logs/greengrass.log";

    std::string logGroupName =
        "/aws/greengrass/" + _logGroup.componentType + "/" + _logGroup.region + "/" +
        _logGroup.componentName;
    std::string logStreamName = "/" + _logStream.date + "/thing/" +
                                std::string(_logStream.thingName.c_str());

    LOG.atInfo().kv("Using log group name", logGroupName).log();
    LOG.atInfo().kv("Using log stream name", logStreamName).log();

    // read log file and build request body
    std::ifstream file(logFilePath);
    if (!file.is_open()) {
        LOG.atInfo().event("Unable to open Greengrass log file").log();
        return;
    }

    std::string logLine;
    rapidjson::Document logEvents;
    logEvents.SetArray();
    // TODO: Request chunking or otherwise enforce log length
    while (getline(file, logLine)) {
        // Reading greengrass.log line by line here, process each line as needed
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

    // TODO: Use ggapi for JSON parsing and creation
    rapidjson::Document logEventsBody;
    logEventsBody.SetObject();
    logEventsBody.AddMember("logStreamName", logStreamName, logEventsBody.GetAllocator());
    logEventsBody.AddMember("logGroupName", logGroupName, logEventsBody.GetAllocator());
    // TODO: Ensure that the batch of log events satisfies all requirements of the API here:
    // https://docs.aws.amazon.com/AmazonCloudWatchLogs/latest/APIReference/API_PutLogEvents.html
    logEventsBody.AddMember("logEvents", logEvents, logEventsBody.GetAllocator());

    rapidjson::Document createLogGroupBody;
    createLogGroupBody.SetObject();
    createLogGroupBody.AddMember("logGroupName", logGroupName,
                                 createLogGroupBody.GetAllocator());

    rapidjson::Document createLogStreamBody;
    createLogStreamBody.SetObject();
    createLogStreamBody.AddMember("logGroupName", logGroupName,
                                  createLogStreamBody.GetAllocator());
    createLogStreamBody.AddMember("logStreamName", logStreamName,
                                  createLogStreamBody.GetAllocator());

    // CreateLogGroup is likely to fail due to the log group already existing. This doesn't block execution and the
    // response is logged in the log file.
    LogManager::makeHTTPCallToCloudwatchLogs("Logs_20140328.CreateLogGroup", createLogGroupBody);
    LogManager::makeHTTPCallToCloudwatchLogs("Logs_20140328.CreateLogStream", createLogStreamBody);
    LogManager::makeHTTPCallToCloudwatchLogs("Logs_20140328.PutLogEvents", logEventsBody);
}
