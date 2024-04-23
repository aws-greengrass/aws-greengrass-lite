#include "cloud_downloader.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return CloudDownloader::get().lifecycle(moduleHandle, phase, data);
}
