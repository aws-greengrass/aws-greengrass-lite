#pragma once
#include <aws/event-stream/event_stream_rpc_server.h>
#include "messaging.hpp"
#include <plugin.hpp>
#include "HeaderValue.hpp"

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
    ggapi::Symbol channel{"channel"};
    ggapi::Symbol socketPath{"domain_socket_path"};
    ggapi::Symbol cliAuthToken{"cli_auth_token"};
    ggapi::Symbol topicName{"aws.greengrass.RequestIpcInfo"};

    static const Keys &get() {
        static Keys keys;
        return keys;
    }
};

static const auto &keys = Keys::get();

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

    static void onContinuationClose(aws_event_stream_rpc_server_continuation_token *token, void *user_data) noexcept;
};
