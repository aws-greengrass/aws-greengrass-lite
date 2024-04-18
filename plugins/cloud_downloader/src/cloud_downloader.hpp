#include <shared_device_sdk.hpp>

#include <logging.hpp>
#include <plugin.hpp>

class CloudDownloader : public ggapi::Plugin {
private:
    void downloadClient(
        Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions,
        const std::string &uriAsString,
        Aws::Crt::Http::HttpRequest &request,
        Aws::Crt::Http::HttpRequestOptions requestOptions,
        Aws::Crt::Allocator *allocator);

    ggapi::Promise fetchToken(ggapi::Symbol, const ggapi::Container &callData);
    void fetchTokenAsync(const ggapi::Struct &callData, ggapi::Promise promise);

    ggapi::Promise genericDownload(ggapi::Symbol, const ggapi::Container &callData);
    void genericDownloadAsync(const ggapi::Struct &callData, ggapi::Promise promise);

    ggapi::Subscription _retrieveArtifactSubs;
    ggapi::Subscription _fetchTesFromCloudSubs;

public:
    void onInitialize(ggapi::Struct data) override;

    static CloudDownloader &get() {
        static CloudDownloader instance{};
        return instance;
    }
};
