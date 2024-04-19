#pragma once
#include <plugin.hpp>
#include <string_view>
#include <temp_module.hpp>
#include <test/temp_dir.hpp>

namespace test {
    class Lifecycle {
        bool _running{false};
        std::string _name;
        ggapi::Plugin &_plugin;
        util::TempModule _module;
        test::TempDir _tempDir;
        ggapi::Struct _configRoot{ggapi::Struct::create()};
        ggapi::Struct _pluginNode{ggapi::Struct::create()};
        ggapi::Struct _pluginNodeConfiguration{ggapi::Struct::create()};
        ggapi::Struct _system{ggapi::Struct::create()};
        ggapi::Struct _services{ggapi::Struct::create()};
        ggapi::Struct _nucleusNode{ggapi::Struct::create()};
        ggapi::Struct _nucleusNodeConfiguration{ggapi::Struct::create()};

        void init() {
            _configRoot.put("system", _system);
            _configRoot.put("services", _services);
            _system.put("rootPath", _tempDir.getDir().generic_string());
            _system.put("thingName", "Test");
            _pluginNode.put("configuration", _pluginNodeConfiguration);
            _nucleusNode.put("componentType", "NUCLEUS");
            _nucleusNode.put("configuration", _nucleusNodeConfiguration);
        }

    public:
        using Plugin = ggapi::Plugin;
        using Events = ggapi::Plugin::Events;

        explicit Lifecycle(std::string_view name, ggapi::Plugin &plugin)
            : _name(name), _plugin(plugin), _module(name) {
            // Mock out configuration
            init();
            // Perform the initialization phase
            auto data = lifecycleData();
            event(Events::INITIALIZE, data);
            // TODO: This should deprecate with recipe parsing
            _name = data.get<std::string>("name");
            _services.put(_name, _pluginNode);
        }
        ~Lifecycle() {
            if(_running) {
                stop();
            }
        }

        ggapi::Plugin &plugin() {
            return _plugin;
        }

        ggapi::Struct &system() {
            return _system;
        }

        ggapi::Struct &config() {
            return _pluginNode;
        }

        ggapi::Struct &nucleus() {
            return _nucleusNode;
        }

        ggapi::Struct lifecycleData() {
            auto data = ggapi::Struct::create();
            data.put(Plugin::MODULE, *_module);
            data.put(Plugin::CONFIG_ROOT, _configRoot);
            data.put(Plugin::SYSTEM, _system);
            data.put(Plugin::NUCLEUS_CONFIG, _nucleusNode);
            data.put(Plugin::CONFIG, _pluginNode);
            data.put(Plugin::NAME, _name);
            return data;
        }

    public:
        bool event(ggapi::Plugin::Events event, ggapi::Struct &data) {
            util::TempModule module(*_module);
            auto eventSymbol = ggapi::Plugin::EVENT_MAP.rlookup(event).value_or(ggapi::Symbol{});
            return _plugin.lifecycle(eventSymbol, data);
        }

        bool event(ggapi::Plugin::Events event) {
            if(event == ggapi::Plugin::Events::START) {
                _running = true;
            }
            auto data = lifecycleData();
            bool handled = this->event(event, data);
            if(event == ggapi::Plugin::Events::STOP) {
                _running = false;
            }
            return handled;
        }

        bool start() {
            return event(Events::START);
        }

        bool stop() {
            return event(Events::STOP);
        }

        bool errorStop() {
            return event(Events::ERROR_STOP);
        }
    };

} // namespace test
