#pragma once

#include "authentication_handler.hpp"
#include <api_archive.hpp>
#include <api_standard_errors.hpp>
#include <cpp_api.hpp>
#include <device_sdk/device_sdk_support.hpp>
#include <plugin.hpp>

namespace ipc_server {

    class ServerListener;
    class ServerConnection;
    class ConnectionStream;
    struct BoundPromise;

    using namespace std::string_view_literals;
    using namespace std::string_literals;

    // NOLINTNEXTLINE("*-exception-escape,*-err58-cpp")
    inline static const auto IPC_NAMESPACE = "aws.greengrass"s;
    // NOLINTNEXTLINE("*-exception-escape,*-err58-cpp")
    inline static const auto IPC_PREFIX = IPC_NAMESPACE + "#"s;

    struct Keys {
    private:
        Keys() = default;

    public:
        ggapi::Symbol terminate{"terminate"};
        ggapi::Symbol contentType{"contentType"};
        ggapi::Symbol serviceModelType{"serviceModelType"};
        ggapi::Symbol shape{"shape"};
        ggapi::Symbol accepted{"accepted"};
        ggapi::Symbol errorCode{"errorCode"};
        ggapi::Symbol error{"error"};
        ggapi::Symbol message{"message"};
        ggapi::Symbol _errorCode{"_errorCode"};
        ggapi::Symbol _message{"_message"};
        ggapi::Symbol _service{"_service"};
        ggapi::Symbol channel{"channel"};
        ggapi::Symbol greengrassIpcServiceName{IPC_PREFIX + "GreengrassCoreIPC"};
        ggapi::Symbol fatal{"fatal"};
        static const Keys &get() noexcept {
            static Keys keys;
            return keys;
        }
    };

    static const auto &keys = Keys::get();

    class IpcServer final : public ggapi::Plugin {
    private:
        // TODO: This needs to come from host-environment plugin
        static constexpr std::string_view SOCKET_NAME = "gglite-ipc.socket";

        ggapi::Struct requestIpcInfoHandler(ggapi::Symbol, const ggapi::Container &);

        mutable std::shared_mutex _mutex;
        ggapi::Struct _system;
        ggapi::Struct _config;
        ggapi::Subscription _ipcInfoSubs;
        util::CheckedSharedPointers<ConnectionStream> _streams;
        util::CheckedSharedPointers<ServerConnection> _connections;
        util::CheckedSharedPointers<ServerListener> _listeners;
        util::CheckedSharedPointers<BoundPromise> _promises;

        std::string _socketPath;

        std::unique_ptr<AuthenticationHandler> _authHandler;
        std::shared_ptr<ServerListener> _activeListener;

    public:
        IpcServer() noexcept;
        void onInitialize(ggapi::Struct data) override;
        void onStart(ggapi::Struct data) override;
        void onStop(ggapi::Struct data) override;

        /**
         * Socket path as string - exposed for testing
         */
        [[nodiscard]] const std::string &socketPath() const {
            return _socketPath;
        }

        static IpcServer &get() {
            static IpcServer instance{};
            return instance;
        }

        static util::CheckedSharedPointers<ServerListener> &listeners() {
            return get()._listeners;
        }

        static util::CheckedSharedPointers<ServerConnection> &connections() {
            return get()._connections;
        }

        static util::CheckedSharedPointers<ConnectionStream> &streams() {
            return get()._streams;
        }

        static util::CheckedSharedPointers<BoundPromise> &promises() {
            return get()._promises;
        }

        static void *beginPromise(
            const ggapi::ModuleScope &module, std::shared_ptr<BoundPromise> &promise);

        static ggapi::Future completePromise(
            void *promiseHandle, const ggapi::Container &value) noexcept;

        static ggapi::Future failPromise(
            void *promiseHandle, const ggapi::GgApiError &err) noexcept;

        static void logFatal(const std::exception_ptr &error, std::string_view text) noexcept;
    };

} // namespace ipc_server
