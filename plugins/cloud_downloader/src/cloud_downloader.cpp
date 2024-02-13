
#include "cloud_downloader.hpp"
#include "aws/crt/Allocator.h"

const int TIME_OUT_MS = 5000;
const int PORT_NUM = 443;

/*
A common client helper function to make the request to a url using the aws's library
*/
void CloudDownloader::downloadClient(
    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions,
    std::string uriAsString,
    Aws::Crt::Http::HttpRequest &request,
    Aws::Crt::Http::HttpRequestOptions requestOptions,
    Aws::Crt::Allocator *allocator) {

    Aws::Crt::ApiHandle apiHandle(allocator);
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
    bool errorOccured = true;
    bool connectionShutdown = false;

    std::condition_variable semaphore;
    std::mutex semaphoreLock;

    auto onConnectionSetup =
        [&](const std::shared_ptr<Aws::Crt::Http::HttpClientConnection> &newConnection,
            int errorCode) {
            std::lock_guard<std::mutex> lockGuard(semaphoreLock);
            if(!errorCode) {
                connection = newConnection;
                errorOccured = false;
            } else {
                connectionShutdown = true;
            }
            semaphore.notify_one();
        };

    auto onConnectionShutdown = [&](Aws::Crt::Http::HttpClientConnection &, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        connectionShutdown = true;
        if(errorCode) {
            errorOccured = true;
        }
        semaphore.notify_one();
    };

    Aws::Crt::Http::HttpClientConnectionOptions httpClientConnectionOptions;
    httpClientConnectionOptions.Bootstrap = &clientBootstrap;
    httpClientConnectionOptions.OnConnectionSetupCallback = onConnectionSetup;
    httpClientConnectionOptions.OnConnectionShutdownCallback = onConnectionShutdown;
    httpClientConnectionOptions.SocketOptions = socketOptions;
    httpClientConnectionOptions.TlsOptions = tlsConnectionOptions;
    httpClientConnectionOptions.HostName = std::string((const char *) hostName.ptr, hostName.len);
    httpClientConnectionOptions.Port = PORT_NUM;

    std::unique_lock<std::mutex> semaphoreULock(semaphoreLock);
    if(!Aws::Crt::Http::HttpClientConnection::CreateConnection(
           httpClientConnectionOptions, allocator)) {
        LOG.atError().log("Failed to create connection");
        throw std::runtime_error("Failed to create connection");
    }
    semaphore.wait(semaphoreULock, [&]() { return connection || connectionShutdown; });

    if(errorOccured || connectionShutdown || !connection) {
        LOG.atError().log("Failed to establish sucessful connection");
        throw std::runtime_error("Failed to establish sucessful connection");
    }

    int responseCode = 0;
    requestOptions.request = &request;

    bool streamCompleted = false;
    requestOptions.onStreamComplete = [&](Aws::Crt::Http::HttpStream &, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        streamCompleted = true;
        if(errorCode) {
            errorOccured = true;
        }
        semaphore.notify_one();
    };
    requestOptions.onIncomingHeadersBlockDone = nullptr;
    requestOptions.onIncomingHeaders = [&](Aws::Crt::Http::HttpStream &stream,
                                           enum aws_http_header_block,
                                           const Aws::Crt::Http::HttpHeader *,
                                           std::size_t) {
        responseCode = stream.GetResponseStatusCode();
    };

    request.SetMethod(Aws::Crt::ByteCursorFromCString("GET"));
    request.SetPath(uri.GetPathAndQuery());

    Aws::Crt::Http::HttpHeader hostHeader;
    hostHeader.name = Aws::Crt::ByteCursorFromCString("host");
    hostHeader.value = uri.GetHostName();
    request.AddHeader(hostHeader);

    auto stream = connection->NewClientStream(requestOptions);
    if(!stream->Activate()) {
        LOG.atError().log("Failed to activate stream and download the file..");
        throw std::runtime_error("Failed to activate stream and download the file..");
    }

    semaphore.wait(semaphoreULock, [&]() { return streamCompleted; });

    connection->Close();
    semaphore.wait(semaphoreULock, [&]() { return connectionShutdown; });

    LOG.atInfo().event("Downlaod Status").kv("response_code", responseCode).log();
}

/*
Generic Http/Https downloader that provides a in memory response for the results from the `url`.
Uses provided device IOT credintials to make the query to the `url`
*/
ggapi::Struct CloudDownloader::fetchToken(ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {
    auto uriAsString = callData.get<std::string>("uri");
    auto thingName = callData.get<std::string>("thingName");
    auto certPath = callData.get<std::string>("certPath");
    auto caPath = callData.get<std::string>("caPath");
    auto caFile = callData.get<std::string>("caFile");
    auto pkeyPath = callData.get<std::string>("pkeyPath");

    std::shared_ptr<Aws::Crt::Http::HttpClientConnection> connection;
    struct aws_allocator *allocator = aws_default_allocator();

    // Setup Connection TLS
    Aws::Crt::Io::TlsContextOptions tlsCtxOptions =
        Aws::Crt::Io::TlsContextOptions::InitClientWithMtls(
            certPath.c_str(), pkeyPath.c_str(), allocator);
    tlsCtxOptions.OverrideDefaultTrustStore(caPath.c_str(), caFile.c_str());

    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    if(tlsContext.GetInitializationError() != AWS_ERROR_SUCCESS) {
        throw std::runtime_error("Failed to create TLS context");
    }
    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    // Setup Connection Request
    Aws::Crt::Http::HttpRequest request;
    std::stringstream downloadContent;

    // Add thingName as header
    Aws::Crt::Http::HttpHeader header;
    header.name = Aws::Crt::ByteCursorFromCString("x-amzn-iot-thingname");
    header.value = Aws::Crt::ByteCursorFromCString(thingName.c_str()); // Add thingname here
    request.AddHeader(header);

    // Callback on sucess request stream response
    Aws::Crt::Http::HttpRequestOptions requestOptions;
    requestOptions.onIncomingBody = [&](Aws::Crt::Http::HttpStream &,
                                        const Aws::Crt::ByteCursor &data) {
        downloadContent.write((const char *) data.ptr, data.len);
    };

    CloudDownloader self;
    self.downloadClient(tlsConnectionOptions, uriAsString, request, requestOptions, allocator);

    LOG.atInfo().event("Download Status").log("Successfully fetched Token...");
    std::cout << "[Cloud_Downloader] Successfully fetched Token... " << std::endl;

    ggapi::Struct response = ggapi::Struct::create();
    response.put("Response", downloadContent.str());
    return response;
}

/*
Generic Http/Https downloader that downlaods to the provided `localPath`
*/
ggapi::Struct CloudDownloader::genericDownload(ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {
    // TODO: Add more Topics support
    auto uriAsString = callData.get<std::string>("uri");
    auto localPath = callData.get<std::string>("localPath");

    std::shared_ptr<Aws::Crt::Http::HttpClientConnection> connection;
    struct aws_allocator *allocator = aws_default_allocator();

    // Setup Connection TLS
    Aws::Crt::Io::TlsContextOptions tlsCtxOptions =
        Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    if(tlsContext.GetInitializationError() != AWS_ERROR_SUCCESS) {
        throw std::runtime_error("Failed to create TLS context");
    }
    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    // Setup Connection Request
    Aws::Crt::Http::HttpRequest request;
    std::ofstream downloadedFile(localPath.c_str(), std::ios_base::binary);
    if(!downloadedFile) {
        LOG.atError().log("Failed to create file");
        throw std::runtime_error("Failed to create file");
    }

    // Callback on sucess request stream response
    Aws::Crt::Http::HttpRequestOptions requestOptions;
    requestOptions.onIncomingBody = [&](Aws::Crt::Http::HttpStream &,
                                        const Aws::Crt::ByteCursor &data) {
        downloadedFile.write((const char *) data.ptr, data.len);
    };

    CloudDownloader self;
    self.downloadClient(tlsConnectionOptions, uriAsString, request, requestOptions, allocator);

    downloadedFile.flush();
    downloadedFile.close();

    LOG.atInfo().event("Downloaded Status").kv("file_name", localPath).log();
    std::cout << "[Cloud_Downloader] Downloaded file " << localPath << std::endl;

    // TODO: Follow the spec for Response
    ggapi::Struct response = ggapi::Struct::create();
    response.put("Response", "Download Complete");
    return response;
}

bool CloudDownloader::onDiscover(ggapi::Struct data) {
    aws_io_library_init(Aws::Crt::DefaultAllocator());
    std::ignore = getScope().subscribeToTopic(
        ggapi::Symbol{"aws.greengrass.retrieve_artifact"}, genericDownload);

    std::ignore = getScope().subscribeToTopic(
        ggapi::Symbol{"aws.greengrass.fetch_TES_from_cloud"}, fetchToken);
    return true;
}

bool CloudDownloader::onRun(ggapi::Struct data) {
    return true;
}

void CloudDownloader::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cout << "[Cloud_Downloader] Running lifecycle phase " << phase.toString() << std::endl;
}

bool CloudDownloader::onTerminate(ggapi::Struct data) {
    return true;
}
