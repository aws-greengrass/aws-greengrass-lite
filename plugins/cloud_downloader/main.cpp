#include <curl/curl.h>

#include <fstream>
#include <iostream>
#include <plugin.hpp>

class CloudDownloader : public ggapi::Plugin {
private:
    ggapi::Buffer localData;
    static bool download();
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);

public:
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;

    bool onRun(ggapi::Struct data) override;

    static ggapi::Struct testListener(
        ggapi::Task task, ggapi::Symbol topic, ggapi::Struct callData);

    static CloudDownloader &get() {
        static CloudDownloader instance{};
        return instance;
    }
};

// Callback function to write data received from libcurl into a file
size_t CloudDownloader::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t totalSize = size * nmemb;
    std::ofstream *file = static_cast<std::ofstream *>(userp);
    file->write(static_cast<char *>(contents), totalSize);
    return totalSize;
}

bool CloudDownloader::download() {
    // Initialize libcurl
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(!curl) {
        std::cerr << "Failed to initialize libcurl" << std::endl;
        return false;
    }
    bool returnStatus = true;

    // Change URL to download the zip file from
    const char *url = nullptr;
    //Temp check
    if(url == nullptr) {
        std::cerr << "URL is null" << std::endl;
        return false;
    }

    // Change Path to save the downloaded zip file
    const char *outputPath = "downloaded.zip";

    // Open the output file stream
    std::ofstream outputFile(outputPath, std::ios::binary);
    if(!outputFile.is_open()) {
        std::cerr << "Failed to open output file" << std::endl;
        returnStatus = false;
    } else {
        // Set libcurl options
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CloudDownloader::WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outputFile);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow HTTP redirects
        // Perform the request
        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            std::cerr << "Failed to download file: " << curl_easy_strerror(res) << std::endl;
            outputFile.close();
            returnStatus = false;
        } else {
            // Cleanup
            outputFile.close();
            curl_easy_cleanup(curl);
            curl_global_cleanup();

            std::cout << "File downloaded and saved to: " << outputPath << std::endl;
        }
    }
    return returnStatus;
}

bool CloudDownloader::onStart(ggapi::Struct data) {
    return true;
}

bool CloudDownloader::onRun(ggapi::Struct data) {
    download();
    return true;
}

void CloudDownloader::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cout << "[S3-Download] Running lifecycle phase " << phase.toString() << std::endl;
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return CloudDownloader::get().lifecycle(moduleHandle, phase, data, pHandled);
}
