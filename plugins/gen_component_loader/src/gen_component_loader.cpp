#include "gen_component_loader.hpp"
#include "c_api.h"
#include "containers.hpp"
#include "handles.hpp"
#include "scopes.hpp"
#include "util.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <temp_module.hpp>
#include <utility>
#include <filesystem>

static const auto LOG = ggapi::Logger::of("gen_component_loader");

static constexpr std::string_view on_path_prefix = "onpath";
static constexpr std::string_view exists_prefix = "exists";

class GenComponentDeligate : public ggapi::Plugin, public util::RefObject<GenComponentDeligate> {
    std::string _name;
    ggapi::Struct _recipe;

 public:
    explicit GenComponentDeligate(const ggapi::Struct& data) {
        //_name = data.get<std::string>("componentName");
        _recipe = data.get<ggapi::Struct>("recipe");
    }

    bool lifecycleCallback(const ggapi::ModuleScope&, ggapi::Symbol event, ggapi::Struct data) {
        return lifecycle(event, std::move(data));
    }

    ggapi::ModuleScope registerComponent() {
        auto module = ggapi::ModuleScope::registerGlobalPlugin(
            _name,
            ggapi::LifecycleCallback::of(
                &GenComponentDeligate::lifecycleCallback, this, baseRef()));
        return module;
    }

    bool onInitialize(ggapi::Struct data) override{
        data.put(NAME, "aws.greengrass.gen_component_deligate");

        //LifecycleSection lifecycleData;

        auto install = data.get<std::string>("install");

        std::cout<<"I was initilized"<<std::endl;
        return true;
    }
};

ggapi::ObjHandle registerGenComponent(ggapi::Symbol, const ggapi::Container &callData) {
    ggapi::Struct data{callData};

    auto newModule = std::make_shared<GenComponentDeligate>(data);

    // TODO:
    ggapi::Struct returnData = ggapi::Struct::create();

    auto module = newModule->registerComponent();

    returnData.put("moduleHandle", module);
    return std::move(returnData);
}

bool GenComponentLoader::onInitialize(ggapi::Struct data) {

    data.put(NAME, "aws.greengrass.gen_component_loader");

    _fetchTesFromCloudSubs = ggapi::Subscription::subscribeToTopic(
        ggapi::Symbol{"componentType::aws.greengrass.generic"},
        ggapi::TopicCallback::of(&GenComponentLoader::registerGenComponent, this));
    return true;
}
