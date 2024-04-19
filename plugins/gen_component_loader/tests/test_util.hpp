#pragma once
#include "gen_component_loader.hpp"

namespace gen_component_loader_test {
    constexpr std::string_view DISCOVER = "discover";
    constexpr std::string_view START = "start";
    constexpr std::string_view RUN = "run";
    constexpr std::string_view TERMINATE = "stop";

    class TestGenComponentLoader : public GenComponentLoader {
        ggapi::ModuleScope _moduleScope;

    public:
        explicit TestGenComponentLoader(ggapi::ModuleScope moduleScope)
            : GenComponentLoader(), _moduleScope(moduleScope) {
            auto init = ggapi::Struct::create().put(ggapi::Plugin::MODULE, _moduleScope);
            internalBind(init);
        }

        bool executePhase(std::string_view phase) {
            bool status = lifecycle(ggapi::Symbol{phase}, ggapi::Struct::create());
            return status;
        };

        bool startLifecycle() {
            // TODO: gotta be a better way to do this
            return executePhase(DISCOVER) && executePhase(START) && executePhase(RUN);
        }

        bool stopLifecycle() {
            return executePhase(TERMINATE);
        }
    };
} // namespace test
