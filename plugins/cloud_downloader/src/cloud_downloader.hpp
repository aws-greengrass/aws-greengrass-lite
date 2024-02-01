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
    bool onTerminate(ggapi::Struct data) override;

    static CloudDownloader &get() {
        static CloudDownloader instance{};
        return instance;
    }
};
