#include "connection_stream.hpp"
#include "bound_promise.hpp"
#include "ipc_server.hpp"
#include <string_util.hpp>
#include <temp_module.hpp>

namespace ipc_server {

    static const auto LOG = // NOLINT(cert-err58-cpp)
        ggapi::Logger::of("com.aws.greengrass.ipc_server.stream");

    AwsToken ConnectionStream::token() const noexcept {
        std::shared_lock guard{_stateMutex};
        auto awsToken = _token.get();
        aws_event_stream_rpc_server_continuation_acquire(awsToken);
        return {aws_event_stream_rpc_server_continuation_release, awsToken};
    }

    void ConnectionStream::initOptions(
        aws_event_stream_rpc_server_stream_continuation_options &options) noexcept {
        options = {};
        options.on_continuation = onContinuation;
        options.on_continuation_closed = onContinuationClose;
        options.user_data = _handle;
    }

    std::shared_ptr<ServerConnection> ConnectionStream::connection() const noexcept {
        std::shared_lock guard{_stateMutex};
        return _connection.lock();
    }

    uintptr_t ConnectionStream::connectionId() const noexcept {
        auto conn = connection();
        if(conn) {
            return conn->id();
        } else {
            return 0;
        }
    }

    void ConnectionStream::onAccept() {
        LOG.atDebug()
            .kv("id", connectionId())
            .kv("token", tokenId())
            .kv("operation", operation())
            .logStream(
                [&](auto &str) { str << "[IPC] Request for " << operation() << " Received\n"; });
    }

    void ConnectionStream::onContinuation(
        aws_event_stream_rpc_server_continuation_token *token,
        const aws_event_stream_rpc_message_args *message_args,
        void *user_data) noexcept {

        // TODO: This invoke and others - handle index exception
        try {
            IpcServer::streams().invoke(
                user_data, &ConnectionStream::onContinuationImpl, token, message_args);
        } catch(...) {
            IpcServer::logFatal(std::current_exception(), "Error trying to dispatch continuation");
        }
    }

    void ConnectionStream::onContinuationImpl(
        aws_event_stream_rpc_server_continuation_token *,
        const aws_event_stream_rpc_message_args *message_args) {

        util::TempModule module(_module);

        try {
            auto message = Message::parse(*message_args);
            LOG.atDebug("continuation")
                .kv("id", connectionId())
                .kv("token", tokenId())
                .logStream([&](auto &str) { str << "Continuation received: " << message; });

            if(message.isTerminateStream() && !message.getPayload()) {
                // Handshake to say stream is being terminated
                _state.store(State::Terminate);
                return;
            }

            auto expected = State::Begin;
            if(!_state.compare_exchange_strong(expected, State::Command)) {
                throw ggapi::UnsupportedOperationError("Only one request message is allowed");
            }
            verifyOperation();
            ggapi::async(&ConnectionStream::dispatchAsync, this, baseRef(), message);
        } catch(...) {
            sendErrorMessage(ggapi::GgApiError::of(std::current_exception()));
        }
    }

    void ConnectionStream::verifyOperation() const {
        if(!util::startsWith(operation(), IPC_PREFIX)) {
            // TODO: We can relax this later. Open question is the contrast between this
            // and the "serviceName" of exceptions
            throw ggapi::UnsupportedOperationError(
                "Only AWS Greengrass namespace operations are supported");
        }
    }

    void ConnectionStream::dispatchAsync(
        const std::shared_ptr<ConnectionStream> &, const Message &message) noexcept {
        try {
            dispatch(message);
        } catch(...) {
            auto err = ggapi::GgApiError::of(std::current_exception());
            LOG.atError("dispatch failed").logError(err);
            sendErrorMessage(err);
        }
    }

    void ConnectionStream::dispatch(const Message &message) {
        auto content = message.getPayload();
        if(content.isStruct()) {
            ggapi::Struct structData{content};
            Header null;
            auto &header = message.findHeader(Header::SERVICE_MODEL_TYPE_HEADER, null);
            if(header && header.isString()) {
                structData.put(keys.serviceModelType, header.toString());
            }
        }

        auto conn = connection();
        if(!conn) {
            throw ggapi::NotConnectedError();
        }
        auto serviceName = conn->getConnectedServiceName();

        if(serviceName.empty()) {
            // IPC call is not associated as a "service", meaning there is no way to
            // check if authorized. Skip checking authorization if so, to unblock IPC tests (which
            // don't run as a "service").

            // TODO: Determine if we want to support IPC calls when not running as a GG "service",
            // if not update IPC tests and throw error here instead.
            ipcCallOperation(content);
            return;
        }
        // Get LPC call meta data needed to make an authorization check
        auto metaFuture = ggapi::Subscription::callTopicFirst(lpcMetaTopic(), content);
        if(!metaFuture) {
            LOG.atDebug("getIpcMetaFailed").log("No IPC meta data handler for "s + lpcMetaTopic());
            // TODO: SECURITY: Before GA, throw exception instead of lpcCallOperation (all ipc
            // operations need to handle authorization).
            ipcCallOperation(content);
        } else {
            metaFuture.whenValid(
                &ConnectionStream::ipcMetaCallback, this, baseRef(), content, serviceName);
        }
    }

    void ConnectionStream::ipcMetaCallback(
        const std::shared_ptr<ConnectionStream> &,
        const ggapi::Container &content,
        const std::string &serviceName,
        const ggapi::Future &future) noexcept {
        try {
            auto metaResp = ggapi::Struct(future.getValue());
            auto request{ggapi::Struct::create()};

            request.put("destination", metaResp.get<std::string>("destination"));
            request.put("principal", serviceName);
            request.put("operation", operation());
            request.put("resource", metaResp.get<std::string>("resource"));
            request.put("resourceType", metaResp.get<std::string>("resourceType"));

            auto authFuture = ggapi::Subscription::callTopicFirst(lpcAuthTopic(), request);
            if(!authFuture) {
                throw ggapi::UnauthorizedError(
                    "No authorization check handler for " + lpcAuthTopic());
            }
            authFuture.whenValid(&ConnectionStream::ipcAuthCallback, this, baseRef(), content);
        } catch(...) {
            auto err = ggapi::GgApiError::of(std::current_exception());
            LOG.atError("ipcMetaFailed").logError(err);
            sendErrorMessage(err);
        }
    }

    void ConnectionStream::ipcAuthCallback(
        const std::shared_ptr<ConnectionStream> &,
        const ggapi::Container &content,
        const ggapi::Future &future) noexcept {
        try {
            auto authResp = ggapi::Struct(future.getValue());
            ipcCallOperation(content);
        } catch(...) {
            auto err = ggapi::GgApiError::of(std::current_exception());
            LOG.atError("ipcAuthFailed").logError(err);
            sendErrorMessage(err);
        }
    }

    void ConnectionStream::ipcCallOperation(const ggapi::Container &content) {
        // TODO: Right now we're passing payload and dropping all the headers
        // Need to restructure in a similar way as the return message
        auto opFuture = ggapi::Subscription::callTopicFirst(lpcTopic(), content);
        if(!opFuture) {
            throw ggapi::UnsupportedOperationError("No handler for "s + lpcTopic());
        }
        auto expected = State::Command;
        if(!_state.compare_exchange_strong(expected, State::Responding)) {
            throw ggapi::UnsupportedOperationError(
                "Illegal internal state: "s + std::to_string(static_cast<int>(expected)));
        }
        opFuture.whenValid(&ConnectionStream::firstResponseAsync, this, baseRef());
    }

    void ConnectionStream::firstResponseAsync(
        const std::shared_ptr<ConnectionStream> &, const ggapi::Future &future) noexcept {
        try {
            firstResponse(future);
        } catch(...) {
            auto err = ggapi::GgApiError::of(std::current_exception());
            LOG.atError("topicDispatchFailed").logError(err);
            sendErrorMessage(err);
        }
    }

    void ConnectionStream::firstResponse(const ggapi::Future &future) {
        auto response = future.getValue();
        ggapi::Channel channel;
        if(response.isStruct()) {
            ggapi::Struct responseStruct(response);
            channel = responseStruct.get<ggapi::Channel>(keys.channel);
            if(!responseStruct.hasKey(keys.serviceModelType)) {
                responseStruct.put(keys.serviceModelType, ipcServiceModel());
            }
            if(!channel) {
                responseStruct.put(keys.terminate, true);
            }
            onReceiveResponse(responseStruct);
            if(channel) {
                connectChannel(channel);
            }
        } else if(response.isChannel()) {
            channel = ggapi::Channel(response);
            connectChannel(channel);
        } else {
            throw ggapi::GgApiError("Internal error - invalid IPC handler response");
        }
    }

    void ConnectionStream::connectChannel(ggapi::Channel channel) {
        // Locked update of _channel
        {
            std::unique_lock guard{_stateMutex};
            _channel = channel;
        }
        // Weak reference allows ConnectionStream to go away while channel is still being
        // written to
        std::weak_ptr<ConnectionStream> weakRef = baseRef();
        channel.addListenCallback(
            ggapi::ChannelListenCallback::of(&ConnectionStream::onChannelCallback, weakRef));
    }

    void ConnectionStream::onChannelCallback(
        const std::weak_ptr<ConnectionStream> &weakSelf, const ggapi::ObjHandle &response) {

        auto self = weakSelf.lock();
        if(!self) {
            LOG.atWarn("droppedMessage").log(std::string("Dropped message on closed continuation"));
            return;
        }

        try {
            if(response.isFuture()) {
                auto future = ggapi::Future::of(response);
                future.whenValid(&ConnectionStream::onChannelCallbackDeferred, weakSelf);
            } else {
                self->onReceiveResponse(ggapi::Struct(response));
            }
        } catch(...) {
            // Error was not wrapped, indicates an error in delegate plugin
            auto err = ggapi::GgApiError::of(std::current_exception());
            LOG.atError("invalidChannelMessage")
                .kv("id", self->connectionId())
                .kv("token", self->tokenId())
                .cause(err)
                .log(std::string("Bad message for channel"));
            self->sendErrorMessage(err);
        }
    }

    void ConnectionStream::onChannelCallbackDeferred(
        const std::weak_ptr<ConnectionStream> &weakSelf, const ggapi::Future &future) noexcept {

        try {
            auto self = weakSelf.lock();
            if(!self) {
                LOG.atWarn("droppedMessage")
                    .log(std::string("Dropped message on closed continuation"));
                return;
            }

            ggapi::Container value;
            try {
                value = future.getValue();
            } catch(...) {
                auto err = ggapi::GgApiError::of(std::current_exception());
                LOG.atWarn("requestFailed")
                    .kv("id", self->connectionId())
                    .kv("token", self->tokenId())
                    .cause(err)
                    .log(std::string("Delegate threw app exception"));
                self->sendErrorMessage(err);
            }
            try {
                self->onReceiveResponse(ggapi::Struct(value));
            } catch(...) {
                auto err = ggapi::GgApiError::of(std::current_exception());
                // Not wrapped in future, so this is an error in delegate plugin
                LOG.atWarn("invalidChannelMessage")
                    .kv("id", self->connectionId())
                    .kv("token", self->tokenId())
                    .cause(err)
                    .log(std::string("Delegate handler failed"));
                self->sendErrorMessage(err);
            }
        } catch(...) {
            // TODO: fix LOG signatures
        }
    }

    void ConnectionStream::onReceiveResponse(const ggapi::Struct &response) {
        ggapi::Container shape;
        std::string serviceModelType;
        bool terminate = false;
        shape = response.get<ggapi::Container>(keys.shape);
        if(response.hasKey(keys.serviceModelType)) {
            serviceModelType = response.get<std::string>(keys.serviceModelType);
        }
        if(response.hasKey(keys.terminate)) {
            terminate = response.get<bool>(keys.terminate);
        }

        Message msg;
        msg.setServiceModelType(serviceModelType);
        msg.setPayloadAndContentType(shape);
        msg.setTerminateStream(terminate);
        sendMessage(msg);
    }

    /**
     * Version of sendMessage for sending a failure, which itself cannot fail
     */
    void ConnectionStream::sendErrorMessage(const ggapi::GgApiError &error) noexcept {
        try {
            auto message = Message::ofError(error);
            message.setTerminateStream();
            sendMessage(message);
        } catch(...) {
            try {
                LOG.atError("sendErrorMessageFailed")
                    .cause(std::current_exception())
                    .log("Unable to respond with error message - closing");
            } catch(...) {
                // Ignore - proceed to close
            }
            connection()->close();
        }
    }

    ggapi::Future ConnectionStream::sendMessage(const Message &message) {
        Message copy(message);
        if(message.isTerminateStream()) {
            _state.store(State::Terminate);
        }
        auto &formatted = copy.prepare();
        std::shared_ptr<BoundPromise> bound;
        AwsToken awsToken = token();
        auto handle = IpcServer::get().beginPromise(module(), bound);
        auto code = aws_event_stream_rpc_server_continuation_send_message(
            awsToken.get(), &formatted, ConnectionStream::onCompleteSend, handle);
        ggapi::Future f;
        if(code) {
            f = IpcServer::failPromise(
                handle, util::AwsSdkError(code, "Stream send failed (initial)"));
        } else {
            f = bound->promise.toFuture();
        }
        f.whenValid([](const ggapi::Future &future) {
            try {
                std::ignore = future.getValue();
                // LOG.atTrace().log("Sent");
            } catch(ggapi::GgApiError &err) {
                LOG.atWarn("streamSendError").cause(err).log(err.what());
            }
        });
        return f;
    }

    void ConnectionStream::onCompleteSend(int error_code, void *user_data) {
        if(error_code) {
            IpcServer::failPromise(
                user_data, util::AwsSdkError(error_code, "Stream send failed (async)"));
        } else {
            IpcServer::completePromise(user_data, {}); // No useful data to convey
        }
    }

    void ConnectionStream::onContinuationClose(
        aws_event_stream_rpc_server_continuation_token *token, void *user_data) noexcept {
        try {
            IpcServer::streams().invoke(
                user_data, &ConnectionStream::onContinuationCloseImpl, token);
        } catch(...) {
            IpcServer::logFatal(
                std::current_exception(), "Error trying to dispatch continuation close");
        }
    }

    void ConnectionStream::onContinuationCloseImpl(
        aws_event_stream_rpc_server_continuation_token *) noexcept {

        util::TempModule module(_module);

        LOG.atInfo("close").logStream(
            [&](auto &str) { str << "Stream ending for " << _operation; });
        IpcServer::streams().erase(_handle);
    }
} // namespace ipc_server
