#include <cpp_api.h>
#include <iostream>

// A layered plugins is permitted to add additional abstract plugins

const ggapi::StringOrd DISCOVER_PHASE{"discover"};

void doDiscoverPhase(ggapi::Scope moduleHandle, ggapi::Struct phaseData);

extern "C" [[maybe_unused]] EXPORT bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data
) noexcept {
    std::cout << "Running layered lifecycle plugins... " << ggapi::StringOrd{phase}.toString()
              << std::endl;
    ggapi::StringOrd phaseOrd{phase};
    if(phaseOrd == DISCOVER_PHASE) {
        doDiscoverPhase(ggapi::Scope{moduleHandle}, ggapi::Struct{data});
    }
    return true;
}

void greengrass_delegate_lifecycle(
    ggapi::Scope moduleHandle, ggapi::StringOrd phase, ggapi::Struct data
) {
    std::cout << "Running lifecycle delegate... " << moduleHandle.getHandleId() << " phase "
              << phase.toString() << std::endl;
}

void doDiscoverPhase(ggapi::Scope moduleHandle, ggapi::Struct phaseData) {
    ggapi::ObjHandle nestedPlugin =
        moduleHandle.registerPlugin(ggapi::StringOrd{"MyDelegate"}, greengrass_delegate_lifecycle);
    // Examples
    phaseData.set("a", 1);
    phaseData.set({
        {"a", 1},
        {"b", 2}
    });
    ggapi::Struct s = ggapi::Scope::thisTask().createStruct().set({
        {"a", 1},
        {"b", 2}
    });
    ggapi::Struct s2 = ggapi::Scope::thisTask().createStruct().put("a", 1).put("b", 2).put("c", 3);
}
