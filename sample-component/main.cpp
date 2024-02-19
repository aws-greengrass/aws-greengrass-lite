#include "aws/greengrass/GreengrassCoreIpcModel.h"
#include <aws/crt/Api.h>
#include <aws/greengrass/GreengrassCoreIpcClient.h>
#include <iostream>
#include <mutex>

using namespace Aws::Crt;
using namespace Aws::Greengrass;

class IpcClientLifecycleHandler : public ConnectionLifecycleHandler {
    void OnConnectCallback() override {
        std::cout << "Connected to Greengrass Lite" << std::endl;
    }

    void OnDisconnectCallback(RpcError error) override {
        std::cout << "Disconnected from Greengrass Lite with " << error.StatusToString()
                  << std::endl;
    }

    bool OnErrorCallback(RpcError error) override {
        std::cout << "Error while processing messages from Greengrass Lite "
                  << error.StatusToString() << std::endl;
        return true;
    }
};

// check that the publish has been received. */
static std::mutex iotReceiveMutex;
static std::condition_variable iotReceiveBarrier;

class IoTSubscribeHandler : public SubscribeToIoTCoreStreamHandler {
    void OnStreamEvent(IoTCoreMessage *response) override {
        auto message = response->GetMessage();

        if(message.has_value() && message.value().GetPayload().has_value()) {
            auto payloadBytes = message.value().GetPayload().value();
            std::string payloadString(payloadBytes.begin(), payloadBytes.end());
            std::cout << "Received payload: " << payloadString << std::endl;
        }

        iotReceiveBarrier.notify_one();
    };
};

static std::mutex localReceiveMutex;
static std::condition_variable localReceiveBarrier;

class LocalSubscribeHandler : public SubscribeToTopicStreamHandler {
    void OnStreamEvent(SubscriptionResponseMessage *response) override {
        auto jsonMessage = response->GetJsonMessage();
        if(jsonMessage.has_value() && jsonMessage.value().GetMessage().has_value()) {
            auto messageString = jsonMessage.value().GetMessage().value().View().WriteReadable();
            std::cout << "Received payload: " << messageString << std::endl;
        } else {
            auto binaryMessage = response->GetBinaryMessage();
            if(binaryMessage.has_value() && binaryMessage.value().GetMessage().has_value()) {
                auto messageBytes = binaryMessage.value().GetMessage().value();
                std::string messageString(messageBytes.begin(), messageBytes.end());
                std::cout << "Received payload: " << messageString << std::endl;
            }
        }

        localReceiveBarrier.notify_one();
    };
};

int main(int argc, char *argv[]) {
    ApiHandle apiHandle(g_allocator);
    Io::EventLoopGroup eventLoopGroup(1);
    Io::DefaultHostResolver socketResolver(eventLoopGroup, 64, 30);
    Io::ClientBootstrap bootstrap(eventLoopGroup, socketResolver);
    IpcClientLifecycleHandler lifecycleHandler;
    GreengrassCoreIpcClient ipcClient(bootstrap);
    auto connectionStatus = ipcClient.Connect(lifecycleHandler).get();
    if(!connectionStatus) {
        std::cerr << "Failed to establish IPC connection: " << connectionStatus.StatusToString()
                  << std::endl;
        exit(-1);
    }

    String message = argv[1];
    static constexpr int TIMEOUT = 10;

    {
        String topic{"my/iot/topic"};

        // Subscribe to IoT core topic
        auto streamHandler = MakeShared<IoTSubscribeHandler>(DefaultAllocator());
        auto subscribeOperation = ipcClient.NewSubscribeToIoTCore(streamHandler);

        SubscribeToIoTCoreRequest subscribeRequest;
        subscribeRequest.SetQos(QOS_AT_LEAST_ONCE);
        subscribeRequest.SetTopicName(topic);

        std::cout << "Attempting to subscribe to topic " << topic << std::endl;
        auto requestStatus = subscribeOperation->Activate(subscribeRequest).get();
        if(!requestStatus) {
            std::cerr << "Failed to send subscription request to " << topic << std::endl;
            exit(-1);
        }

        auto subscribeResultFuture = subscribeOperation->GetResult();
        auto subscribeResult = subscribeResultFuture.get();
        if(subscribeResult) {
            std::cout << "Successfully subscribed to " << topic << std::endl;
        } else {
            auto errorType = subscribeResult.GetResultType();
            if(errorType == OPERATION_ERROR) {
                OperationError *error = subscribeResult.GetOperationError();
                if(error->GetMessage().has_value())
                    std::cerr << "Greengrass Core responded with an error: "
                              << error->GetMessage().value() << std::endl;
            } else {
                std::cerr
                    << "Attempting to receive the response from the server failed with error code "
                    << subscribeResult.GetRpcError().StatusToString() << std::endl;
            }
        }

        // Publish to the same IoT core topic
        QOS qos = QOS_AT_LEAST_ONCE;
        PublishToIoTCoreRequest publishRequest;
        Vector<uint8_t> messageData({message.begin(), message.end()});
        publishRequest.SetTopicName(topic);
        publishRequest.SetPayload(messageData);
        publishRequest.SetQos(qos);

        auto publishOperation = ipcClient.NewPublishToIoTCore();
        requestStatus = publishOperation->Activate(publishRequest, nullptr).get();

        if(!requestStatus) {
            std::cerr << "Failed to publish to " << topic
                      << "with error: " << requestStatus.StatusToString() << std::endl;
            exit(-1);
        }

        auto publishResponseFuture = publishOperation->GetResult();
        if(publishResponseFuture.wait_for(std::chrono::seconds(TIMEOUT))
           == std::future_status::timeout) {
            std::cerr << "Operation timed out while waiting for response from Greengrass Core."
                      << std::endl;
            exit(-1);
        }

        auto publishResult = publishResponseFuture.get();
        if(publishResult) {
            std::cout << "Successfully published to topic " << topic << std::endl;
            auto *response = publishResult.GetOperationResponse();
            (void) response;
        } else {
            auto errorType = publishResult.GetResultType();
            if(errorType == OPERATION_ERROR) {
                auto *error = publishResult.GetOperationError();
                if(error->GetMessage().has_value()) {
                    std::cerr << "Greengrass responded with an error "
                              << error->GetMessage().value() << std::endl;
                }
            } else {
                std::cerr
                    << "Attempting to receive the response from the server failed with error code "
                    << publishResult.GetRpcError().StatusToString() << std::endl;
            }
        }

        std::unique_lock guard{iotReceiveMutex};
        iotReceiveBarrier.wait(guard);
    }

    {
        String topic{"my/local/topic"};

        auto streamHandler = MakeShared<LocalSubscribeHandler>(DefaultAllocator());
        auto subscribeOperation = ipcClient.NewSubscribeToTopic(streamHandler);

        SubscribeToTopicRequest subscribeRequest;
        subscribeRequest.SetTopic(topic);

        std::cout << "Attempting to subscribe to topic " << topic << std::endl;
        auto requestStatus = subscribeOperation->Activate(subscribeRequest).get();
        if(!requestStatus) {
            std::cerr << "Failed to send subscription request to " << topic << std::endl;
            exit(-1);
        }

        auto subscribeResultFuture = subscribeOperation->GetResult();
        auto subscribeResult = subscribeResultFuture.get();
        if(subscribeResult) {
            std::cout << "Successfully subscribed to " << topic << std::endl;
        } else {
            auto errorType = subscribeResult.GetResultType();
            if(errorType == OPERATION_ERROR) {
                OperationError *error = subscribeResult.GetOperationError();
                if(error->GetMessage().has_value())
                    std::cerr << "Greengrass Core responded with an error: "
                              << error->GetMessage().value() << std::endl;
            } else {
                std::cerr
                    << "Attempting to receive the response from the server failed with error code "
                    << subscribeResult.GetRpcError().StatusToString() << std::endl;
            }
        }

        PublishToTopicRequest publishRequest;
        Vector<uint8_t> messageData({message.begin(), message.end()});
        BinaryMessage binaryMessage;
        binaryMessage.SetMessage(messageData);
        PublishMessage publishMessage;
        publishMessage.SetBinaryMessage(binaryMessage);
        publishRequest.SetTopic(topic);
        publishRequest.SetPublishMessage(publishMessage);

        auto publishOperation = ipcClient.NewPublishToTopic();
        requestStatus = publishOperation->Activate(publishRequest, nullptr).get();

        if(!requestStatus) {
            std::cerr << "Failed to publish to " << topic
                      << "with error: " << requestStatus.StatusToString() << std::endl;
            exit(-1);
        }

        auto publishResponseFuture = publishOperation->GetResult();
        if(publishResponseFuture.wait_for(std::chrono::seconds(TIMEOUT))
           == std::future_status::timeout) {
            std::cerr << "Operation timed out while waiting for response from Greengrass Core."
                      << std::endl;
            exit(-1);
        }

        auto publishResult = publishResponseFuture.get();
        if(publishResult) {
            std::cout << "Successfully published to topic " << topic << std::endl;
            auto *response = publishResult.GetOperationResponse();
            (void) response;
        } else {
            auto errorType = publishResult.GetResultType();
            if(errorType == OPERATION_ERROR) {
                auto *error = publishResult.GetOperationError();
                if(error->GetMessage().has_value()) {
                    std::cerr << "Greengrass responded with an error "
                              << error->GetMessage().value() << std::endl;
                }
            } else {
                std::cerr
                    << "Attempting to receive the response from the server failed with error code "
                    << publishResult.GetRpcError().StatusToString() << std::endl;
            }
        }

        std::unique_lock guard{localReceiveMutex};
        localReceiveBarrier.wait(guard);
    }
    return 0;
}
