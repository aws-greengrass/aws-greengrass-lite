#include <cpp_api.h>
#include <iostream>

// A layered plugins is permitted to add additional abstract plugins

const ggapi::StringOrd DISCOVER_PHASE {"discover"};

void doDiscoverPhase(ggapi::Scope moduleHandle, ggapi::Struct phaseData);

extern "C" [[maybe_unused]] EXPORT bool greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
    std::cout << "Running layered lifecycle plugins... " << ggapi::StringOrd{phase}.toString() << std::endl;
    ggapi::StringOrd phaseOrd{phase};
    if (phaseOrd == DISCOVER_PHASE) {
        doDiscoverPhase(ggapi::Scope{moduleHandle}, ggapi::Struct{data});
    }
    return true;
}

void greengrass_delegate_lifecycle(ggapi::Scope moduleHandle, ggapi::StringOrd phase, ggapi::Struct data) {
    std::cout << "Running lifecycle delegate... " << moduleHandle.getHandleId() << " phase " << phase.toString() << std::endl;
}

void doDiscoverPhase(ggapi::Scope moduleHandle, ggapi::Struct phaseData) {
    ggapi::ObjHandle nestedPlugin = moduleHandle.registerPlugin(ggapi::StringOrd{"MyDelegate"}, greengrass_delegate_lifecycle);
}
