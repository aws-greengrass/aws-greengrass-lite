#include <plugin.hpp>
#include "device_provisioning.hpp"
#include <thread>

struct Keys {
    ggapi::StringOrd start{"start"};
    ggapi::StringOrd run{"run"};
    ggapi::StringOrd topicName{"aws.greengrass.provisioning"};
    ggapi::StringOrd retain{"retain"};
    ggapi::StringOrd userProperties{"userProperties"};
    ggapi::StringOrd messageExpiryIntervalSeconds{"messageExpiryIntervalSecon"
                                                  "ds"};
    ggapi::StringOrd correlationData{"correlationData"};
    ggapi::StringOrd responseTopic{"responseTopic"};
    ggapi::StringOrd payloadFormat{"payloadFormat"};
    ggapi::StringOrd contentType{"contentType"};

    static const Keys &get() {
        static std::unique_ptr<Keys> keyRef;
        if(keyRef == nullptr) {
            keyRef = std::make_unique<Keys>();
        }
        return *keyRef;
    }
};

//class ListenerStub : public pubsub::AbstractCallback {
//    data::Environment &_env;
//    std::string _flagName;
//    std::shared_ptr<data::StructModelBase> _returnData;
//
//public:
//    ListenerStub(
//        data::Environment &env,
//        const std::string_view &flagName,
//        const std::shared_ptr<data::StructModelBase> &returnData
//    )
//        : _env(env), _flagName(flagName), _returnData{returnData} {
//    }
//
//    ListenerStub(data::Environment &env, const std::string_view &flagName)
//        : _env(env), _flagName(flagName) {
//    }
//
//    data::ObjHandle operator()(
//        data::ObjHandle taskHandle, data::StringOrd topicOrd, data::ObjHandle dataStruct
//    ) override {
//        std::shared_ptr<data::TrackingScope> scope =
//            _env.handleTable.getObject<data::TrackingScope>(taskHandle);
//        std::string topic = _env.stringTable.getStringOr(topicOrd, "(anon)"s);
//        std::shared_ptr<data::StructModelBase> data;
//        if(dataStruct) {
//            data = _env.handleTable.getObject<data::StructModelBase>(dataStruct);
//            data->put(_flagName, topic);
//        }
//        if(_returnData) {
//            _returnData->put(
//                "_" + _flagName,
//                data::StructElement{std::static_pointer_cast<data::ContainerModelBase>(data)}
//            );
//            return scope->anchor(_returnData).getHandle();
//        } else {
//            return {};
//        }
//    }
//};

extern "C" bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t dataHandle
) noexcept {
    return ProvisionPlugin::get().lifecycle(moduleHandle, phase, dataHandle);
}
