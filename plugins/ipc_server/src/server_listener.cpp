#include "server_listener.hpp"
#include "ipc_server.hpp"
#include "server_connection.hpp"
#include <temp_module.hpp>

namespace ipc_server {

    static const auto LOG = // NOLINT(cert-err58-cpp)
        ggapi::Logger::of("com.aws.greengrass.ipc_server.listener");

    void ServerListener::connect(const std::string &socket_path) {
        // TODO: This should be refactored again into a new class
        if(std::filesystem::exists(socket_path)) {
            std::filesystem::remove(socket_path);
        }

        aws_event_stream_rpc_server_listener_options listenerOptions = {};
        listenerOptions.host_name = socket_path.c_str();
        listenerOptions.port = 0;
        listenerOptions.socket_options = &_socketOpts.GetImpl();
        listenerOptions.bootstrap = _bootstrap.GetUnderlyingHandle();
        listenerOptions.on_new_connection = ServerListener::onNewServerConnection;
        listenerOptions.on_connection_shutdown = ServerListener::onServerConnectionShutdown;
        listenerOptions.on_destroy_callback = ServerListener::onListenerDestroy;
        listenerOptions.user_data = _handle;
        _listener.set(aws_event_stream_rpc_server_new_listener(_allocator, &listenerOptions));
        if(!_listener) {
            LOG.atError("connect-error")
                .logAndThrow(util::AwsSdkError("Failed to create IPC server"));
        }
        LOG.atDebug("connect").log("Listening for IPC connections");
    }

    void ServerListener::close() noexcept {
        _closing.store(true); // prevent new connections

        // Snapshot existing connections
        std::vector<std::shared_ptr<ServerConnection>> connections;
        {
            std::shared_lock guard{_stateMutex};
            for(const auto &connection : _connections) {
                connections.push_back(connection.second);
            }
        }
        // And then close
        for(auto &connectionToClose : connections) {
            connectionToClose->close();
        }

        if(_listener) {
            LOG.atDebug("disconnect").log("Disconnected IPC server");
            std::unique_lock guard{_stateMutex};
            _listener.release();
        }
    }

    int ServerListener::onNewServerConnection(
        aws_event_stream_rpc_server_connection *awsConnection,
        int error_code,
        aws_event_stream_rpc_connection_options *connection_options,
        void *user_data) noexcept {

        try {
            return IpcServer::listeners().invoke(
                user_data,
                &ServerListener::onNewServerConnectionImpl,
                awsConnection,
                error_code,
                connection_options);
        } catch(...) {
            IpcServer::logFatal(
                std::current_exception(), "Error trying to dispatch new server connection");
            return AWS_OP_ERR;
        }
    }

    int ServerListener::onNewServerConnectionImpl(
        aws_event_stream_rpc_server_connection *awsConnection,
        int error_code,
        aws_event_stream_rpc_connection_options *connection_options) noexcept {

        util::TempModule tempModule{module()};

        if(error_code) {
            // SDK is simply reporting a connection failure (awsConnection should be nullptr)
            // Caller will take care of de-refing awsConnection
            util::AwsSdkError err(error_code, "Connection request failed");
            LOG.atError().cause(err).log("Connection request failed");
            return AWS_OP_ERR;
        }

        if(_closing.load()) {
            // We are shutting down, so we can just close the connection
            LOG.atWarn().log("Closing: rejecting incoming connection");
            return AWS_OP_ERR;
        }

        // Add-ref to connection ref count to account for us making a copy of the connection
        aws_event_stream_rpc_server_connection_acquire(awsConnection);
        AwsConnection refConnection(aws_event_stream_rpc_server_connection_release, awsConnection);

        try {
            auto managed =
                std::make_shared<ServerConnection>(baseRef(), module(), std::move(refConnection));
            managed->setHandleRef(IpcServer::connections().addAsPtr(managed));
            managed->initOptions(*connection_options);

            {
                std::unique_lock guard{_stateMutex};
                // Connections associated with this listener
                _connections.emplace(awsConnection, managed);
            }

            LOG.atDebug().kv("id", managed->id()).log("Incoming connection");
            return AWS_OP_SUCCESS;
        } catch(...) {
            LOG.atError().cause(std::current_exception()).log("Exception while connecting");
            return AWS_OP_ERR;
        }
    }

    void ServerListener::onServerConnectionShutdown(
        aws_event_stream_rpc_server_connection *awsConnection,
        int error_code,
        void *user_data) noexcept {

        try {
            IpcServer::listeners().invoke(
                user_data,
                &ServerListener::onServerConnectionShutdownImpl,
                awsConnection,
                error_code);
        } catch(...) {
            IpcServer::logFatal(
                std::current_exception(), "Error trying to dispatch server connection shutdown");
        }
    }

    void ServerListener::onServerConnectionShutdownImpl(
        aws_event_stream_rpc_server_connection *awsConnection, int error_code) noexcept {

        util::TempModule tempModule{module()};

        std::shared_ptr<ServerConnection> connection;

        {
            std::unique_lock guard{_stateMutex};
            auto ent = _connections.find(awsConnection);
            if(ent == _connections.end()) {
                LOG.atError().log("Connection not found");
                return;
            }
            connection = ent->second;
            _connections.erase(ent);
        }
        connection->onShutdown(error_code);
    }

    void ServerListener::removeConnection(
        aws_event_stream_rpc_server_connection *awsConnection) noexcept {

        std::unique_lock guard{_stateMutex};
        _connections.erase(awsConnection);
    }

    void ServerListener::onListenerDestroy(
        aws_event_stream_rpc_server_listener *, void *user_data) noexcept {

        IpcServer::listeners().erase(user_data);
    }

} // namespace ipc_server
