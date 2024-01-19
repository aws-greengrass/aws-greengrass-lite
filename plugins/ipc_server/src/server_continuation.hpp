#pragma once
#include <aws/event-stream/event_stream_rpc_server.h>
#include <plugin.hpp>

#include "HeaderValue.hpp"

class ServerContinuation {
public:
    using Token = aws_event_stream_rpc_server_continuation_token;

private:
    Token *_token;
    std::string _operation;
    ggapi::Channel _channel{};
    using ContinutationHandle = std::shared_ptr<ServerContinuation> *;

public:
    explicit ServerContinuation(Token *token, std::string operation)
        : _token{token}, _operation{std::move(operation)} {
    }

    ~ServerContinuation() noexcept {
        if(_channel) {
            _channel.close();
            _channel.release();
        }
    }

    Token *GetUnderlyingHandle() {
        return _token;
    }

    [[nodiscard]] std::string lpcTopic() const {
        return "IPC::" + _operation;
    }

    [[nodiscard]] std::string ipcServiceModel() const {
        return _operation + "Response";
    }

    static ggapi::Struct onTopicResponse(
        const std::weak_ptr<ServerContinuation> &weakSelf, ggapi::Struct response);

    static void onContinuation(
        aws_event_stream_rpc_server_continuation_token *token,
        const aws_event_stream_rpc_message_args *message_args,
        void *user_data) noexcept;

    static void onContinuationClose(
        aws_event_stream_rpc_server_continuation_token *token, void *user_data) noexcept;
};
