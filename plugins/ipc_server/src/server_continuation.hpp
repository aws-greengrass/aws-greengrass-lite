#pragma once
#include <aws/event-stream/event_stream_rpc_server.h>
#include <plugin.hpp>

#include "HeaderValue.hpp"

class ServerContinuationCCallbacks;

class ServerContinuation {
    friend ServerContinuationCCallbacks;

public:
    using Token = aws_event_stream_rpc_server_continuation_token;

private:
    ggapi::ModuleScope _module;
    Token *_token;
    std::string _operation;
    ggapi::Channel _channel{};

public:
    explicit ServerContinuation(ggapi::ModuleScope module, Token *token, std::string operation)
        : _module{std::move(module)}, _token{token}, _operation{std::move(operation)} {
    }

    ~ServerContinuation() noexcept {
        if(_channel) {
            _channel.close();
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

    [[nodiscard]] ggapi::ModuleScope module() const {
        return _module;
    }

    static ggapi::Struct onTopicResponse(
        const std::weak_ptr<ServerContinuation> &weakSelf, const ggapi::Struct &response);
};

extern "C" class ServerContinuationCCallbacks {
    using ContinutationHandle = std::shared_ptr<ServerContinuation> *;

public:
    static void onContinuation(
        aws_event_stream_rpc_server_continuation_token *token,
        const aws_event_stream_rpc_message_args *message_args,
        void *user_data) noexcept;

    static void onContinuationClose(
        aws_event_stream_rpc_server_continuation_token *token, void *user_data) noexcept;
};
