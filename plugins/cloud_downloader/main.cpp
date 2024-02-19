#include "cloud_downloader.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return CloudDownloader::get().lifecycle(moduleHandle, phase, data, pHandled);
}
