#include "log_manager.hpp"
#include "futures.hpp"
#include <fstream>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <temp_module.hpp>

const auto LOG = ggapi::Logger::of("LogManager");

bool LogManager::onInitialize(ggapi::Struct data) {
    // TODO: retrieve and process system config
    std::unique_lock guard{_mutex};
    _nucleus = data.getValue<ggapi::Struct>({"nucleus"});
    _system = data.getValue<ggapi::Struct>({"system"});
    return true;
}

bool LogManager::onStart(ggapi::Struct data) {
    // TODO: subscribe to LPC topics and register log uploading callback
    retrieveCredentialsFromTES();
    if (_credentials.hasKey("Response")) {
        // setup callback to upload logs
        //TODO: how to register this to loop and run constantly
        _logUploadPromise = _logUploadPromise.later(300, &LogManager::processLogsAndUpload);
    } else {
        LOG.atError().log("Could not retrieve credentials from TES");
        return false; // not sure if we should be doing this
    }
    return true;
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
    auto tesFuture = ggapi::Subscription::callTopicFirst(
        ggapi::Symbol{TES_REQUEST_TOPIC}, request);
    if(tesFuture) {
        _credentials = ggapi::Struct(tesFuture.waitAndGetValue());
    }
    _credentials = {};
    _credentials.toJson().write(std::cout);
}


void LogManager::setupClient(const std::string &uriAsString, const rapidjson::Document& requestBody) {
    auto allocator = Aws::Crt::DefaultAllocator();
    aws_io_library_init(allocator);

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

    std::condition_variable conditionalVar;
    std::mutex semaphoreLock;

    auto onConnectionSetup =
        [&](const std::shared_ptr<Aws::Crt::Http::HttpClientConnection> &newConnection,
            int errorCode) {
            util::TempModule tempModule{getModule()};
            std::lock_guard<std::mutex> lockGuard(semaphoreLock);
            if(!errorCode) {
                LOG.atDebug().log("Successful on establishing connection.");
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

    // Setup request
    Aws::Crt::Http::HttpRequest request;
    Aws::Crt::Http::HttpRequestOptions requestOptions;

    int responseCode = 0;
    requestOptions.request = &request;

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
        responseCode = stream.GetResponseStatusCode();
    };

    request.SetMethod(Aws::Crt::ByteCursorFromCString("POST"));
    // TODO cloudwatch uri?
    request.SetPath(uri.GetPathAndQuery());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    requestBody.Accept(writer);
    const char* requestBodyStr = buffer.GetString();

    std::istringstream istream(requestBodyStr);
    //TODO: how to make an IStream???
    std::shared_ptr<Aws::Crt::Io::IStream> bodyStream = std::make_shared<Aws::Crt::Io::IStream>(istream);

    request.SetBody(bodyStream);

    Aws::Crt::Http::HttpHeader hostHeader;
    hostHeader.name = Aws::Crt::ByteCursorFromCString("host");
    // TODO cloudwatch uri -- maybe don't need uri?
    // host name = logs.<region>.<domain>
    hostHeader.value = uri.GetHostName();
    request.AddHeader(hostHeader);

    Aws::Crt::Http::HttpHeader actionHeader;
    actionHeader.name = Aws::Crt::ByteCursorFromCString("Action");
    actionHeader.value = Aws::Crt::ByteCursorFromCString("PutLogEvents");
    request.AddHeader(actionHeader);

    //TODO: try this if action above doesn't work
//    Aws::Crt::Http::HttpHeader actionHeader;
//    actionHeader.name = Aws::Crt::ByteCursorFromCString("X-Amz-Target");
//    actionHeader.value = Aws::Crt::ByteCursorFromCString("Logs_20140328.PutLogEvents");
//    request.AddHeader(actionHeader);

    Aws::Crt::Http::HttpHeader versionHeader;
    versionHeader.name = Aws::Crt::ByteCursorFromCString("Version");
    versionHeader.value = Aws::Crt::ByteCursorFromCString("2014-03-28");
    request.AddHeader(versionHeader);

    //TODO: date hopefully not needed
//    Aws::Crt::Http::HttpHeader dateHeader;
//    dateHeader.name = Aws::Crt::ByteCursorFromCString("x-amz-date");
//    // need to add value
//    request.AddHeader(dateHeader);

    Aws::Crt::Http::HttpHeader authHeader;
    authHeader.name = Aws::Crt::ByteCursorFromCString("Authorization");
    // TODO sigv4
    request.AddHeader(authHeader);

    Aws::Crt::Http::HttpHeader contentHeader;
    contentHeader.name = Aws::Crt::ByteCursorFromCString("Content-Type");
    contentHeader.value = Aws::Crt::ByteCursorFromCString("application/x-amz-json-1.1");
    request.AddHeader(contentHeader);

    //TODO: not sure if needed, also we aren't chunking right now
//    Aws::Crt::Http::HttpHeader lengthHeader;
//    lengthHeader.name = Aws::Crt::ByteCursorFromCString("Content-length");
//    const std::string chunkSize = std::to_string(CHUNK_SIZE);
//    lengthHeader.value = Aws::Crt::ByteCursorFromCString(chunkSize.c_str());
//    request.AddHeader(lengthHeader);

    Aws::Crt::Http::HttpHeader connectionHeader;
    connectionHeader.name = Aws::Crt::ByteCursorFromCString("Connection");
    connectionHeader.value = Aws::Crt::ByteCursorFromCString("Keep-Alive");
    request.AddHeader(connectionHeader);

    auto stream = connection->NewClientStream(requestOptions);
    if(!stream->Activate()) {
        LOG.atError().log("Failed to activate stream and upload logs");
        throw std::runtime_error("Failed to activate stream and upload logs");
    }

    conditionalVar.wait(semaphoreULock, [&]() { return streamCompleted; });

    connection->Close();
    conditionalVar.wait(semaphoreULock, [&]() { return connectionShutdown; });

    LOG.atInfo().event("Upload Status").kv("response_code", responseCode).log();
}

void LogManager::processLogsAndUpload() {
    // TODO: remove hardcoded values
    auto system = _system;
    _logGroup.componentType = "GreengrassSystemComponent";
    _logGroup.componentName = "System";
    _logStream.thingName = system.getValue<Aws::Crt::String>({THING_NAME});
    auto logFilePath = system.getValue<std::string>({"rootpath"})
                       + "/logs/greengrass.log";

    //TODO: set region
    std::string logGroupName =
        "/aws/greengrass/" + _logGroup.componentType + "/" + _logGroup.region + "/" +
        _logGroup.componentName;
    std::string logStreamName = "/" + _logStream.date + "/thing/" +
                                std::string(_logStream.thingName.c_str(),
                                            _logStream.thingName.size());

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
        rapidjson::Document inputLogEvent;
        readLog.Parse(logLine.c_str());
        inputLogEvent.AddMember("timestamp", readLog["timestamp"],
                                inputLogEvent.GetAllocator());
        inputLogEvent.AddMember("message", logLine.c_str(),
                                inputLogEvent.GetAllocator());
        logEvents.PushBack(inputLogEvent, logEvents.GetAllocator());
    }

    //TODO: don't know if log stream and log group are created automatically if they don't already exist
    rapidjson::Document body;
    body.AddMember("logStreamName", logStreamName.c_str(), body.GetAllocator());
    body.AddMember("logGroupName", logGroupName.c_str(), body.GetAllocator());
    body.AddMember("logEvents", logEvents, body.GetAllocator());

    // Callback on success request stream response
    //TODO: verify - response body for put log events call contains "next sequence token" and "rejected log events", token not needed, currently not checking for rejected logs
//    std::stringstream downloadContent;
//    Aws::Crt::Http::HttpRequestOptions requestOptions;
//    requestOptions.onIncomingBody = [&](Aws::Crt::Http::HttpStream &,
//                                        const Aws::Crt::ByteCursor &data) {
//        downloadContent.write((const char *) data.ptr, data.len);
//    };

    //TODO: uri stuff
    // uri host name format: logs.<region>.<domain>
    // need to figure out the path and query field called for on line 176
    LogManager::setupClient(body);


}
