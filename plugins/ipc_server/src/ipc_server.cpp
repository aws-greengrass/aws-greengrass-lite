#include "ipc_server.hpp"
#include "bound_promise.hpp"
#include "server_listener.hpp"
#include "temp_module.hpp"
#include <interfaces/ipc_auth_info.hpp>

static const auto LOG = // NOLINT(cert-err58-cpp)
    ggapi::Logger::of("com.aws.greengrass.ipc_server");

namespace ipc_server {

    IpcServer::IpcServer() noexcept : _authHandler(std::make_shared<AuthenticationHandler>()) {
    }

    /**
     * Module is initialized, but not yet active. Configuration can be read and cached, other
     * data prepared.
     */
    void IpcServer::onInitialize(ggapi::Struct data) {
        std::ignore = util::getDeviceSdkApiHandle(); // Make sure Api initialized
        data.put(NAME, "aws.greengrass.ipc_server");
        std::unique_lock guard{_mutex};
        _system = data.getValue<ggapi::Struct>({"system"});
        _config = data.getValue<ggapi::Struct>({"config"});
    }

    /**
     * Module is to be started. Return once listeners have been registered. Once returned,
     * module is ready to start receiving messages.
     */
    void IpcServer::onStart(ggapi::Struct data) {
        {
            std::unique_lock guard{_mutex};

            // The path for the socket may be explicitly specified in config, or if not, is
            // inferred from root directory.
            if(_system.hasKey("ipcSocketPath")) {
                _socketPath = _system.get<std::string>("ipcSocketPath");
            } else {
                std::filesystem::path filePath =
                    std::filesystem::canonical(_system.getValue<std::string>({"rootPath"}))
                    / SOCKET_NAME;
                _socketPath = filePath.string();
            }

            // This LPC topic is used to allow another plugin (e.g. Generic Component plugin,
            // or CLI plugin) to obtain credentials for a component or other identity "service"
            _ipcInfoSubs = ggapi::Subscription::subscribeToTopic(
                interfaces::ipc_auth_info::interfaceTopic,
                ggapi::TopicCallback::of(&IpcServer::requestIpcInfoHandler, this));
        }

        // Register IPC listener. Note that in implementation, we use the checked-pointers mechanism
        // to detect any pointer errors early. Future compile option in checked-pointers will
        // allow this to be optimized out.
        auto listener = std::make_shared<ServerListener>(getModule());
        listener->setHandleRef(_listeners.addAsPtr(listener));
        try {
            // TODO: Make non-blocking
            listener->connect(_socketPath);
        } catch(...) {
            throw ggapi::GgApiError::of(std::current_exception());
        }
    }

    /**
     * Allow another plugin to obtain information about the IPC connection
     */
    ggapi::Struct IpcServer::requestIpcInfoHandler(ggapi::Symbol, const ggapi::Container &req) {

        interfaces::ipc_auth_info::IpcAuthInfoIn inData;
        ggapi::deserialize(req, inData);

        interfaces::ipc_auth_info::IpcAuthInfoOut outData;
        {
            std::unique_lock guard{_mutex};
            outData.socketPath = _socketPath;
            outData.authToken = _authHandler->generateAuthToken(inData.serviceName).value();
        }
        return ggapi::serialize(outData);
    }

    void IpcServer::onStop(ggapi::Struct structData) {
        _listeners.invokeAll(&ServerListener::close);
    }

    void IpcServer::logFatal(const std::exception_ptr &error, std::string_view text) noexcept {
        util::TempModule module(get().getModule());
        try {
            LOG.atError(keys.fatal).cause(error).log(text);
        } catch(...) {
            // Nothing we can do if logging fails
            // TODO: Improve logging to not throw exceptions
        }
    }

    void *IpcServer::beginPromise(
        const ggapi::ModuleScope &module, std::shared_ptr<BoundPromise> &promise) {
        if(!promise) {
            promise = std::make_shared<BoundPromise>(module, ggapi::Promise::create());
        }
        return promises().addAsPtr(promise);
    }
    ggapi::Future IpcServer::completePromise(
        void *promiseHandle, const ggapi::Container &value) noexcept {
        try {
            auto bound = promises().at(promiseHandle);
            if(bound) {
                util::TempModule module(bound->module);
                bound->promise.setValue(value);
                promises().erase(promiseHandle);
                return bound->promise.toFuture();
            } else {
                return {};
            }
        } catch(...) {
            logFatal(std::current_exception(), "Exception while trying to complete a promise");
            return {};
        }
    }
    ggapi::Future IpcServer::failPromise(
        void *promiseHandle, const ggapi::GgApiError &err) noexcept {
        auto bound = promises().at(promiseHandle);
        try {
            if(bound) {
                util::TempModule module(bound->module);
                bound->promise.setError(err);
                promises().erase(promiseHandle);
                return bound->promise.toFuture();
            } else {
                return {};
            }
        } catch(...) {
            logFatal(std::current_exception(), "Exception while trying to fail a promise");
            return {};
        }
    }
} // namespace ipc_server
