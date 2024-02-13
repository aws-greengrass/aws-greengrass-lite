#include "native_plugin.hpp"

#include "env.hpp"
#include "startable.hpp"

#include "tasks_subscriptions.hpp"

static const logging::LoggerBase<ggapi::LoggingTraits> &getLogger() {
    static const auto LOG = ggapi::Logger::of("com.aws.greengrass.lifecycle.CommandLine");
    return LOG;
}

ggapi::Struct NativePlugin::startProcessListener(
    ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {
    auto requiresPrivilege = callData.get<bool>("RequiresPrivilege");
    auto script = callData.get<std::string>("Script");

    auto identifier = [callData, script]() {
        if(callData.hasKey("identifier")) {
            return callData.get<std::string>("identifier");
        } else {
            using namespace std::string_literals;
            return "Process"s;
        }
    }();

    using namespace std::string_literals;

    std::string container_uri = "http://localhost:8090/2016-11-01/credentialprovider/";

    auto &startable =
        ipc::Startable{}
            .withCommand(_shell)
            .withEnvironment({
                {std::string{ipc::PATH_ENVVAR}, ipc::getEnviron(ipc::PATH_ENVVAR)},
                // TODO: auth token per Component
                {"SVCUID"s, _authToken},
                {"AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT"s, _socketPath},
                {"AWS_CONTAINER_CREDENTIALS_FULL_URI"s, container_uri},
                {"AWS_CONTAINER_AUTHORIZATION_TOKEN"s, _authToken},
            })
            // TODO: Windows "run raw script" switch
            .withArguments({"-c", std::move(script)})
            // TODO: allow output to pass back to caller if subscription is specified
            .withOutput([identifier](util::Span<const char> buffer) {
                std::cout << '[' << identifier << "]: ";
                std::cout.write(buffer.data(), static_cast<std::streamsize>(buffer.size())) << '\n';
            })
            .withError([identifier](util::Span<const char> buffer) {
                std::cerr << '[' << identifier << "]: ";
                std::cerr.write(buffer.data(), static_cast<std::streamsize>(buffer.size())) << '\n';
            });

    if(callData.hasKey("onComplete")) {
        auto subscription = callData.get<ggapi::Subscription>("onComplete");
        startable.withCompletion([callback = getScope().anchor(subscription)](int returnCode) {
            std::ignore = callback.call(ggapi::Struct::create().put("returnCode", returnCode));
        });
    }
    if(!requiresPrivilege && _user.has_value()) {
        startable.asUser(*_user);
        if(_group.has_value()) {
            startable.asGroup(*_group);
        }
    }

    std::unique_ptr<ipc::Process> proc{};
    try {
        proc = startable.start();
    } catch(const std::exception &e) {
        getLogger().atError().event("process-start-error").log(e.what());
    }

    auto response = ggapi::Struct::create();
    response.put("status", proc != nullptr);
    if(proc) {
        auto processId = _manager.registerProcess(std::move(proc));
        response.put("processId", processId.id);
    }
    return response;
}

bool NativePlugin::onBind(ggapi::Struct data) {
    _system = getScope().anchor(data.getValue<ggapi::Struct>({"system"}));
    _nucleus = getScope().anchor(data.getValue<ggapi::Struct>({"nucleus"}));
    return true;
}

bool NativePlugin::onStart(ggapi::Struct data) {
    auto nucleusConfig = _nucleus.load();
    // TODO: handle WindowsDeviceConfiguration
    auto &&runWithDefault =
        nucleusConfig.getValue<ggapi::Struct>({"configuration", "runWithDefault"});

    if(runWithDefault.hasKey("posixShell")) {
        _shell = runWithDefault.get<std::string>("posixShell");
    } else {
        // TODO: support library should determine a better default
        _shell = "/bin/sh";
    }
    auto userGroup = runWithDefault.get<std::string>("posixUser");
    if(!userGroup.empty()) {
        auto it = userGroup.find(':');
        if(it == std::string::npos) {
            _user = std::move(userGroup);
        } else {
            _user = userGroup.substr(0, it);
            _group = userGroup.substr(it + 1);
        }
    }
    std::ignore = getScope().subscribeToTopic(
        keys.startProcessTopic,
        ggapi::TopicCallback::of(&NativePlugin::startProcessListener, this));
    return true;
}

bool NativePlugin::onRun(ggapi::Struct data) {
    auto request = ggapi::Struct::create();
    request.put(keys.serviceName, SERVICE_NAME);
    auto result = ggapi::Task::sendToTopic(keys.infoTopicName, request);
    _socketPath = result.get<std::string>(keys.socketPath);
    _authToken = result.get<std::string>(keys.cliAuthToken);
    return true;
}

bool NativePlugin::onBootstrap(ggapi::Struct structData) {
    structData.put(NAME, keys.serviceName);
    return true;
}

void NativePlugin::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cout << "[native-plugin] Running lifecycle phase " << phase.toString() << std::endl;
}
