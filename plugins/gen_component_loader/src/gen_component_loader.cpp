#include "gen_component_loader.hpp"
#include "c_api.h"
#include "containers.hpp"
#include "handles.hpp"
#include "scopes.hpp"
#include "util.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <temp_module.hpp>
#include <utility>

static const auto LOG = ggapi::Logger::of("gen_component_loader");

static constexpr std::string_view on_path_prefix = "onpath";
static constexpr std::string_view exists_prefix = "exists";

class GenComponentDelegate : public ggapi::Plugin, public util::RefObject<GenComponentDelegate> {
    std::string _name;
    ggapi::Struct _recipe;

public:
    explicit GenComponentDelegate(const ggapi::Struct &data) {
        _recipe = data.get<ggapi::Struct>("recipe");
        _name = data.get<std::string>("componentName");
    }
    // self-> To store a count to the class's object itself
    //           so that the Delegate remains in memory event after the GenComponentLoader returns
    //       self is passed as const as the reference count for the class itself should not be
    //       increased any further.
    static bool lifecycleCallback(
        const std::shared_ptr<GenComponentDelegate> &self,
        ggapi::ModuleScope,
        ggapi::Symbol event,
        ggapi::Struct data) {
        return self->lifecycle(event, std::move(data));
    }

    ggapi::ModuleScope registerComponent() {
        // baseRef() enables the class to be able to point to itself
        auto module = ggapi::ModuleScope::registerGlobalPlugin(
            _name,
            ggapi::LifecycleCallback::of(&GenComponentDelegate::lifecycleCallback, baseRef()));
        return module;
    }

    bool onInitialize(ggapi::Struct data) override {
        data.put(NAME, "aws.greengrass.gen_component_Delegate");

        auto install = _recipe.get<std::string>("Lifecycle");

        // ggapi::Archive::transform<ggapi::ContainerDearchiver>(data, src);

        std::cout << "I was initialized" << std::endl;
        return true;
    }
};

ggapi::ObjHandle GenComponentLoader::registerGenComponent(
    ggapi::Symbol, const ggapi::Container &callData) {
    ggapi::Struct data{callData};

    auto newModule = std::make_shared<GenComponentDelegate>(data);

    // TODO:
    ggapi::Struct returnData = ggapi::Struct::create();

    auto module = newModule->registerComponent();

    returnData.put("moduleHandle", module);
    return std::move(returnData);
}

bool GenComponentLoader::onInitialize(ggapi::Struct data) {

    data.put(NAME, "aws.greengrass.gen_component_loader");

    _delegateComponentSubscription = ggapi::Subscription::subscribeToTopic(
        ggapi::Symbol{"componentType::aws.greengrass.generic"},
        ggapi::TopicCallback::of(&GenComponentLoader::registerGenComponent, this));

    // Notify nucleus that this plugin supports loading generic components
    auto request{ggapi::Struct::create()};
    request.put("componentSupportType", "aws.greengrass.generic");
    request.put("componentSupportTopic", "componentType::aws.greengrass.generic");
    auto future =
        ggapi::Subscription::callTopicFirst(ggapi::Symbol{"aws.greengrass.componentType"}, request);
    auto response = ggapi::Struct(future.waitAndGetValue());

    return true;
}
