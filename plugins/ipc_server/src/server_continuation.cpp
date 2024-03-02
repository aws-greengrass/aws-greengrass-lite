#include "server_continuation.hpp"
#include "ipc_server.hpp"

ggapi::Struct ServerContinuation::onTopicResponse(
    const std::weak_ptr<ServerContinuation> &weakSelf, const ggapi::Struct &response) {
    // TODO: unsubscribe
    auto self = weakSelf.lock();
    if(!self) {
        return ggapi::Struct::create();
    }

    auto messageType = response.hasKey(keys.errorCode) && response.get<int>(keys.errorCode) != 0
                           ? AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_ERROR
                           : AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_MESSAGE;

    int32_t flags = (response.hasKey(keys.terminate) && response.get<bool>(keys.terminate))
                        ? AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM
                        : 0;

    auto json = response.hasKey(keys.shape) ? response.get<ggapi::Struct>(keys.shape).toJson()
                                            : ggapi::Struct::create().toJson();

    auto serviceModel = response.get<std::string>(keys.serviceModelType);
    if(serviceModel.size() > std::numeric_limits<uint16_t>::max()) {
        // TODO: Error handling
        serviceModel.resize(std::numeric_limits<uint16_t>::max());
    }

    const auto sender = [self = std::move(self)](auto *args) {
        return aws_event_stream_rpc_server_continuation_send_message(
            self->GetUnderlyingHandle(), args, onMessageFlush, nullptr);
    };

    using namespace std::string_literals;
    auto contentType = response.hasKey(keys.contentType)
                           ? response.get<std::string>(keys.contentType)
                           : ContentType::JSON;
    if(contentType.size() > std::numeric_limits<uint16_t>::max()) {
        // TODO: Error handling
        contentType.resize(std::numeric_limits<uint16_t>::max());
    }

    std::array headers{
        makeHeader(Headers::ServiceModelType, Headervaluetypes::stringbuffer{serviceModel}),
        makeHeader(Headers::ContentType, Headervaluetypes::stringbuffer(contentType))};
    int result = sendMessage(sender, headers, json, messageType, flags);
    if(result != AWS_OP_SUCCESS) {
        ggapi::Buffer payload =
            ggapi::Buffer::create().put(0, std::string_view{"InternalServerError"});
        std::array errorHeaders{
            makeHeader(Headers::ContentType, Headervaluetypes::stringbuffer(ContentType::Text))};
        sendMessage(
            sender,
            headers,
            payload,
            AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_ERROR,
            AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM);
    }
    return ggapi::Struct::create();
}

extern "C" void ServerContinuationCCallbacks::onContinuation(
    aws_event_stream_rpc_server_continuation_token *,
    const aws_event_stream_rpc_message_args *message_args,
    void *user_data) noexcept {
    // TODO: This code needs to correctly handle exceptions and turn them into IPC errors
    std::cerr << "[IPC] Continuation received:\n" << *message_args << '\n';

    if(message_args->message_flags & AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM) {
        std::cerr << "Stream terminating\n";
        return;
    }

    using namespace ggapi;

    auto json = [message_args] {
        auto jsonHandle =
            Buffer::create()
                .insert(-1, util::Span{message_args->payload->buffer, message_args->payload->len})
                .fromJson();
        return jsonHandle.getHandleId() ? jsonHandle.unbox<Struct>() : Struct::create();
    }();
    auto continuation = *static_cast<ContinutationHandle>(user_data);
    auto responseFuture = ggapi::Subscription::callTopicFirst(continuation->lpcTopic(), json);
    ggapi::Struct response;
    if(responseFuture) {
        // TODO: Take advantage of LPC Future "andThen" rather than blocking
        response = ggapi::Struct(responseFuture.waitAndGetValue());
    }
    if(!response || response.empty()) {
        std::cerr << "[IPC] LPC appears unhandled\n";
        const auto sender = [continuation](auto *args) {
            return aws_event_stream_rpc_server_continuation_send_message(
                continuation->GetUnderlyingHandle(), args, onMessageFlush, nullptr);
        };
        std::string message = R"({ "error": "LPC unhandled", "message": ")"
                              "LPC unhandled.\" }";
        ggapi::Buffer payload = ggapi::Buffer::create().put(0, std::string_view{message});
        std::array headers{
            makeHeader(Headers::ContentType, Headervaluetypes::stringbuffer(ContentType::JSON))};
        sendMessage(
            sender,
            headers,
            payload,
            AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_ERROR,
            AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM);
    } else {
        response.put(keys.serviceModelType, continuation->ipcServiceModel());
        ServerContinuation::onTopicResponse(continuation, response);
        if(response.hasKey(keys.channel)) {
            auto channel = response.get<ggapi::Channel>(keys.channel);
            continuation->_channel = channel;
            auto continuationWeak = std::weak_ptr{continuation};
            channel.addListenCallback(ggapi::ChannelListenCallback::of(
                &ServerContinuation::onTopicResponse, continuationWeak));
        }
    }
}

extern "C" void ServerContinuationCCallbacks::onContinuationClose(
    aws_event_stream_rpc_server_continuation_token *, void *user_data) noexcept {
    auto continuation = static_cast<ContinutationHandle>(user_data);
    std::cerr << "Stream ending for " << (*continuation)->_operation << std::endl;
    // TODO: Improve in this, per memory coding standards
    // NOLINTNEXTLINE(*-owning-memory)
    delete continuation;
}
