#include "ipc_server.hpp"
#include <catch2/catch_all.hpp>
#include <test/plugin_lifecycle.hpp>

#include <aws/crt/Api.h>
#include <aws/greengrass/GreengrassCoreIpcClient.h>
#include <filesystem>

// NOLINTBEGIN

namespace test {
    using namespace Aws::Crt;
    using namespace Aws::Greengrass;

    class Client : public ConnectionLifecycleHandler {
        inline static const auto MAX_HOSTS = 64;
        inline static const auto MAX_TTL = 30;
        Io::EventLoopGroup _eventLoopGroup{1};
        Io::DefaultHostResolver _socketResolver{_eventLoopGroup, MAX_HOSTS, MAX_TTL};
        Io::ClientBootstrap _bootstrap{_eventLoopGroup, _socketResolver};
        GreengrassCoreIpcClient _ipcClient{_bootstrap};

    public:
        Client() = default;
        ~Client() noexcept {
            close();
        }
        Client(const Client &) = delete;
        Client(Client &&) = delete;
        Client &operator=(const Client &) = delete;
        Client &operator=(Client &&) = delete;

        static ConnectionConfig config(std::string_view path, std::string_view auth) {
            ConnectionConfig cfg{};
            cfg.SetHostName(String(path));
            cfg.SetPort(0);
            StringStream authTokenPayloadSS;
            // Acceptable in test, do not do this in production
            authTokenPayloadSS << R"({"authToken":")" << String(auth) << R"("})";
            cfg.SetConnectAmendment(
                MessageAmendment(ByteBufFromCString(authTokenPayloadSS.str().c_str())));
            Io::SocketOptions socketOptions;
            socketOptions.SetSocketDomain(Io::SocketDomain::Local);
            socketOptions.SetSocketType(Io::SocketType::Stream);
            cfg.SetSocketOptions(std::move(socketOptions));
            return cfg;
        }

        static ConnectionConfig config() {
            auto reqData = ggapi::Struct::create().put("serviceName", "test");
            ggapi::Future resp =
                ggapi::Subscription::callTopicFirst(ipc_server::keys.requestIpcInfoTopic, reqData);
            if(!resp) {
                throw std::runtime_error("IPC Info Topic not registered");
            }
            auto respData = resp.getValue();
            if(!respData) {
                throw std::runtime_error("IPC Info Topic did not return data");
            }
            auto respStruct = ggapi::Struct(respData);
            auto socketPath = respStruct.get<std::string>("domain_socket_path");
            if(socketPath.empty()) {
                throw std::runtime_error("IPC Info Topic did not return socket");
            }
            auto socketAuth = respStruct.get<std::string>("cli_auth_token");
            if(socketAuth.empty()) {
                throw std::runtime_error("IPC Info Topic did not return auth token");
            }
            return config(socketPath, socketAuth);
        }

        void connect(const ConnectionConfig &config) {
            // Create the IPC client.
            auto connectionStatus = _ipcClient.Connect(*this, config).get();
            if(!connectionStatus) {
                if(connectionStatus.crtError != 0) {
                    throw util::AwsSdkError(connectionStatus.crtError);
                } else {
                    throw std::runtime_error(std::string(connectionStatus.StatusToString()));
                }
            }
        }

        void close() noexcept {
            _ipcClient.Close();
        }

        [[nodiscard]] GreengrassCoreIpcClient *operator->() {
            return &_ipcClient;
        }

        void OnConnectCallback() override {
        }
        void OnDisconnectCallback(RpcError status) override {
        }
        bool OnErrorCallback(RpcError status) override {
            return true;
        }
        void OnPingCallback(
            const List<EventStreamHeader> &headers, const Optional<ByteBuf> &payload) override {
        }
    };

    class MySubscribeToTopicStreamHandler : public SubscribeToTopicStreamHandler {
    public:
        std::atomic_int counter{0};
        std::atomic_bool closed{false};
        std::condition_variable cv;
        void OnStreamEvent(SubscriptionResponseMessage *response) override {
            counter++;
        }
        void OnStreamClosed() override {
            closed = true;
            cv.notify_all();
        }
    };

} // namespace test

using namespace ipc_server;
using namespace test;

SCENARIO("IPC Server Operations", "[ipcServer]") {
    GIVEN("Initiate the plugin") {
        // start the lifecycle
        auto &plugin = IpcServer::get();
        Lifecycle lifecycle{"aws.greengrass.ipc_server", plugin};

        AND_GIVEN("Server started") {
            lifecycle.start();

            WHEN("When retrieving metadata") {
                auto config = Client::config();
                THEN("socket path is correct") {
                    REQUIRE(config.GetHostName().has_value());
                    REQUIRE(std::string(config.GetHostName().value()) == plugin.socketPath());
                }
            }
            WHEN("Making IPC connection") {
                Client client;
                client.connect(Client::config());
                THEN("Connection is successful") {
                    REQUIRE(client->IsConnected());
                }
                AND_WHEN("Server is stopped while client is connected") {
                    lifecycle.stop();
                    THEN("Requests will fail") {
                        String topic{"my/topic"};
                        String message{"Hello world"};
                        PublishToTopicRequest request;
                        Vector<uint8_t> messageData({message.begin(), message.end()});
                        BinaryMessage binaryMessage;
                        binaryMessage.SetMessage(messageData);
                        PublishMessage publishMessage;
                        publishMessage.SetBinaryMessage(binaryMessage);
                        request.SetTopic(topic);
                        request.SetPublishMessage(publishMessage);
                        auto operation = client->NewPublishToTopic();
                        auto activate = operation->Activate(request, nullptr);
                        auto aStatus = activate.get();
                        THEN("Activate is not successful") {
                            REQUIRE_FALSE(aStatus);
                        }
                    }
                }
                AND_WHEN("Making a simple request that's not handled") {
                    String topic{"my/topic"};
                    String message{"Hello world"};
                    PublishToTopicRequest request;
                    Vector<uint8_t> messageData({message.begin(), message.end()});
                    BinaryMessage binaryMessage;
                    binaryMessage.SetMessage(messageData);
                    PublishMessage publishMessage;
                    publishMessage.SetBinaryMessage(binaryMessage);
                    request.SetTopic(topic);
                    request.SetPublishMessage(publishMessage);
                    auto operation = client->NewPublishToTopic();
                    auto activate = operation->Activate(request, nullptr);
                    auto aStatus = activate.get();
                    THEN("Activate is successful") {
                        CHECK(aStatus);
                        REQUIRE(aStatus.crtError == 0);
                    }
                    THEN("Response is unsuccessful") {
                        auto respFuture = operation->GetResult();
                        auto response = respFuture.get();
                        CHECK_FALSE(response);
                    }
                }
                AND_WHEN("Making a simple request that's handled") {
                    auto publishToTopic = ggapi::Subscription::subscribeToTopic(
                        "IPC::aws.greengrass#PublishToTopic",
                        [](const ggapi::Symbol &,
                           const ggapi::Container &data) -> ggapi::ObjHandle {

                            REQUIRE(data);
                            ggapi::Struct s(data);
                            auto topic = s.get<std::string>("topic");
                            REQUIRE_FALSE(topic.empty());
                            auto publishMessage = s.get<ggapi::Container>("publishMessage");
                            REQUIRE(publishMessage);
                            // Making it async flushes out any assumption of immediate completion
                            return ggapi::Promise::create().async([](ggapi::Promise p) -> void {
                                p.fulfill([]()->ggapi::Struct {
                                    auto resp = ggapi::Struct::create();
                                    auto shape = ggapi::Struct::create();
                                    resp.put("shape", shape);
                                    return resp;
                                });
                            });
                        });
                    String topic{"my/topic"};
                    String message{"Hello world"};
                    PublishToTopicRequest request;
                    Vector<uint8_t> messageData({message.begin(), message.end()});
                    BinaryMessage binaryMessage;
                    binaryMessage.SetMessage(messageData);
                    PublishMessage publishMessage;
                    publishMessage.SetBinaryMessage(binaryMessage);
                    request.SetTopic(topic);
                    request.SetPublishMessage(publishMessage);
                    auto operation = client->NewPublishToTopic();
                    auto activate = operation->Activate(request, nullptr);
                    auto aStatus = activate.get();
                    THEN("Activate is successful") {
                        CHECK(aStatus);
                        REQUIRE(aStatus.crtError == 0);
                    }
                    THEN("Response is successful") {
                        auto respFuture = operation->GetResult();
                        auto response = respFuture.get();
                        CHECK(response);
                    }
                }
                AND_WHEN("Making a simple request that results in an error") {
                    auto publishToTopic = ggapi::Subscription::subscribeToTopic(
                        "IPC::aws.greengrass#PublishToTopic",
                        [](const ggapi::Symbol &,
                           const ggapi::Container &data) -> ggapi::ObjHandle {
                            return ggapi::Promise::create().fulfill([&]() -> ggapi::Struct {
                                throw ggapi::GgApiError("Expected");
                            });
                        });
                    String topic{"my/topic"};
                    String message{"Hello world"};
                    PublishToTopicRequest request;
                    Vector<uint8_t> messageData({message.begin(), message.end()});
                    BinaryMessage binaryMessage;
                    binaryMessage.SetMessage(messageData);
                    PublishMessage publishMessage;
                    publishMessage.SetBinaryMessage(binaryMessage);
                    request.SetTopic(topic);
                    request.SetPublishMessage(publishMessage);
                    auto operation = client->NewPublishToTopic();
                    auto activate = operation->Activate(request, nullptr);
                    auto aStatus = activate.get();
                    THEN("Activate is successful") {
                        CHECK(aStatus);
                        REQUIRE(aStatus.crtError == 0);
                    }
                    THEN("Response is unsuccessful") {
                        // TODO: How do we get error?
                        auto respFuture = operation->GetResult();
                        auto response = respFuture.get();
                        CHECK_FALSE(response);
                    }
                }
                AND_WHEN("Making a streamed request") {

                    std::string topic{"my/topic"};
                    std::string messageType{"aws.greengrass#SubscriptionResponseMessage"};
                    std::string serviceModelType("serviceModelType");
                    std::string shape("shape");

                    auto channel = ggapi::Channel::create();
                    auto s1 = ggapi::Struct::create();
                    s1.put("topic", topic);
                    s1.put("publishMessage", ggapi::Buffer::create());
                    auto d1 = ggapi::Struct::create();
                    d1.put(shape, s1);
                    d1.put(serviceModelType, messageType);
                    channel.write(d1); // Not wrapped in future

                    auto s2 = ggapi::Struct::create();
                    s2.put("topic", topic);
                    s2.put("publishMessage", ggapi::Buffer::create());
                    auto d2 = ggapi::Struct::create();
                    d2.put(shape, s2);
                    d2.put(serviceModelType, messageType);
                    auto f2 = ggapi::Future::of(d2);
                    channel.write(f2); // Wrapped in future

                    auto s3 = ggapi::Struct::create();
                    s3.put("topic", topic);
                    s3.put("publishMessage", ggapi::Buffer::create());
                    auto d3 = ggapi::Struct::create();
                    d3.put(shape, s1);
                    d3.put(serviceModelType, messageType);
                    d3.put("terminate", true); // force IPC to terminate stream
                    channel.write(d3); // Not wrapped in future

                    std::condition_variable cv;
                    channel.addCloseCallback([&cv]() { cv.notify_all(); });

                    auto publishToTopic = ggapi::Subscription::subscribeToTopic(
                        "IPC::aws.greengrass#SubscribeToTopic",
                        [&channel](const ggapi::Symbol &, const ggapi::Container &data)
                            -> ggapi::ObjHandle {
                            return ggapi::Promise::create().fulfill([&]() -> ggapi::Struct {
                                auto resp = ggapi::Struct::create();
                                auto shape = ggapi::Struct::create();
                                resp.put("shape", shape);
                                resp.put("channel", channel);
                                return resp;
                            });
                        });
                    SubscribeToTopicRequest request;
                    request.SetTopic(String{topic});
                    request.SetReceiveMode(
                        Aws::Greengrass::ReceiveMode::RECEIVE_MODE_RECEIVE_ALL_MESSAGES);
                    auto streamHandler = std::make_shared<test::MySubscribeToTopicStreamHandler>();
                    auto operation = client->NewSubscribeToTopic(streamHandler);
                    auto activate = operation->Activate(request, nullptr);
                    auto aStatus = activate.get();
                    THEN("Activate is successful") {
                        CHECK(aStatus);
                        REQUIRE(aStatus.crtError == 0);
                    }
                    THEN("Initial response is successful") {
                        auto respFuture = operation->GetResult();
                        auto response = respFuture.get();
                        CHECK(response);
                        AND_THEN("All streamed data is received") {
                            std::mutex mutex;
                            std::unique_lock<std::mutex> lock(mutex);
                            streamHandler->cv.wait_for(lock, std::chrono::seconds(1));
                            REQUIRE(streamHandler->counter == 3);
                        }
                        AND_THEN("Stream was closed") {
                            std::mutex mutex;
                            std::unique_lock<std::mutex> lock(mutex);
                            REQUIRE(
                                streamHandler->cv.wait_for(lock, std::chrono::seconds(1))
                                == std::cv_status::no_timeout);
                        }
                        AND_THEN("Channel was closed") {
                            std::mutex mutex;
                            std::unique_lock<std::mutex> lock(mutex);
                            REQUIRE(
                                cv.wait_for(lock, std::chrono::seconds(1))
                                == std::cv_status::no_timeout);
                        }
                    }
                }
                AND_WHEN("Server is stopped while client is connected") {
                    lifecycle.stop();
                    THEN("Requests will fail") {
                        String topic{"my/topic"};
                        String message{"Hello world"};
                        PublishToTopicRequest request;
                        Vector<uint8_t> messageData({message.begin(), message.end()});
                        BinaryMessage binaryMessage;
                        binaryMessage.SetMessage(messageData);
                        PublishMessage publishMessage;
                        publishMessage.SetBinaryMessage(binaryMessage);
                        request.SetTopic(topic);
                        request.SetPublishMessage(publishMessage);
                        auto operation = client->NewPublishToTopic();
                        auto activate = operation->Activate(request, nullptr);
                        auto aStatus = activate.get();
                        THEN("Activate is not successful") {
                            REQUIRE_FALSE(aStatus);
                        }
                    }
                }
            }
        }
    }
}

// NOLINTEND
