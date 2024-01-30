#include <aws/crt/Api.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Uri.h>

#include <fstream>
#include <iostream>
#include <logging.hpp>
#include <mutex>
#include <plugin.hpp>

const auto LOG = ggapi::Logger::of("Cloud_downloader");

class CloudDownloader : public ggapi::Plugin {
private:
    static ggapi::Struct download(ggapi::Task, ggapi::Symbol, ggapi::Struct callData);

public:
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;

    bool onRun(ggapi::Struct data) override;

    static CloudDownloader &get() {
        static CloudDownloader instance{};
        return instance;
    }
};

ggapi::Struct CloudDownloader::download(ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {
    // TODO: Add more Topics support
    auto uriAsString = callData.get<std::string>("uri");
    auto localPath = callData.get<std::string>("localPath");

    struct aws_allocator *allocator = aws_default_allocator();
    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::ByteCursor urlCursor = Aws::Crt::ByteCursorFromCString(uriAsString.c_str());
    Aws::Crt::Io::TlsContextOptions tlsCtxOptions =
        Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    if(!tlsContext) {
        throw std::runtime_error("Failed to create TLS context");
    }

    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    Aws::Crt::Io::Uri uri(urlCursor, allocator);

    auto hostName = uri.GetHostName();
    tlsConnectionOptions.SetServerName(hostName);

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(5000);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    if(!eventLoopGroup) {
        LOG.atError().log("Failed to create event loop group");
        throw std::runtime_error("Failed to create event loop group");
    }
    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    if(!defaultHostResolver) {
        LOG.atError().log("Failed to create default host resolver");
        throw std::runtime_error("Failed to create default host resolver");
    }
    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    if(!clientBootstrap) {
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
    httpClientConnectionOptions.Port = 443;

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

    std::ofstream downloadedFile(localPath.c_str(), std::ios_base::binary);

    if(!downloadedFile) {
        LOG.atError().log("Failed to create file");
        throw std::runtime_error("Failed to create file");
    }

    int responseCode = 0;
    Aws::Crt::Http::HttpRequest request;
    Aws::Crt::Http::HttpRequestOptions requestOptions;
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
    requestOptions.onIncomingBody = [&](Aws::Crt::Http::HttpStream &,
                                        const Aws::Crt::ByteCursor &data) {
        downloadedFile.write((const char *) data.ptr, data.len);
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

    downloadedFile.flush();
    downloadedFile.close();

    LOG.atInfo()
        .event("Downloaded file")
        .kv("file_name", localPath)
        .kv("response_code", responseCode)
        .log();
    std::cout << "[Cloud_Downloader] Downloaded file " << localPath << std::endl;

    ggapi::Struct response = ggapi::Struct::create();
    response.put("Download Complete", uriAsString);
    return response;
}

bool CloudDownloader::onStart(ggapi::Struct data) {
    std::ignore =
        getScope().subscribeToTopic(ggapi::Symbol{"aws.grengrass.retrieve_artifact"}, download);

    return true;
}

bool CloudDownloader::onRun(ggapi::Struct data) {
    return true;
}

void CloudDownloader::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cout << "[Cloud_Downloader] Running lifecycle phase " << phase.toString() << std::endl;
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return CloudDownloader::get().lifecycle(moduleHandle, phase, data, pHandled);
}
