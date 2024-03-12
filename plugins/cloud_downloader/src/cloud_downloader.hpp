#include <aws/crt/Api.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Uri.h>

#include <logging.hpp>
#include <plugin.hpp>

const auto LOG = ggapi::Logger::of("Cloud_downloader");

class CloudDownloader : public ggapi::Plugin {
private:
    static void downloadClient(
        Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions,
        const std::string &uriAsString,
        Aws::Crt::Http::HttpRequest &request,
        Aws::Crt::Http::HttpRequestOptions requestOptions,
        Aws::Crt::Allocator *allocator);

    static ggapi::Struct fetchToken(ggapi::Task, ggapi::Symbol, ggapi::Struct callData);

    static ggapi::Struct genericDownload(ggapi::Task, ggapi::Symbol, ggapi::Struct callData);

public:
    bool onInitialize(ggapi::Struct data) override;

    static CloudDownloader &get() {
        static CloudDownloader instance{};
        return instance;
    }
};
