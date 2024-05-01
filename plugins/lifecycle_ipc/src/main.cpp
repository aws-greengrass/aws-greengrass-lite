#include "lifecycle_ipc.hpp"

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
        ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
return LifecycleIPC::get().lifecycle(moduleHandle, phase, data);
}
