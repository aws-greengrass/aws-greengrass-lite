#include <iostream>

#include <aws/crt/Api.h>
#include <aws/greengrass/GreengrassCoreIpcClient.h>

using namespace Aws::Crt;
using namespace Aws::Greengrass;

class IpcClientLifecycleHandler : public ConnectionLifecycleHandler {
    void OnConnectCallback() override {
    }

    void OnDisconnectCallback(RpcError error) override {
    }

    bool OnErrorCallback(RpcError error) override {
        return true;
    }
};

int main() {
    ApiHandle apiHandle(g_allocator);
    Io::EventLoopGroup eventLoopGroup(1);
    Io::DefaultHostResolver socketResolver(eventLoopGroup, 64, 30);
    Io::ClientBootstrap bootstrap(eventLoopGroup, socketResolver);
    IpcClientLifecycleHandler ipcLifecycleHandler;
    GreengrassCoreIpcClient ipcClient(bootstrap);
    auto connectionStatus = ipcClient.Connect(ipcLifecycleHandler).get();
    if(!connectionStatus) {
        std::cerr << "Failed to establish IPC connection: " << connectionStatus.StatusToString()
                  << std::endl;
        exit(-1);
    }

    String topic("my/topic");
    String message("Hello, World!");
    int timeout = 10;

    {
        PublishToTopicRequest request;
        Vector<uint8_t> messageData({message.begin(), message.end()});
        BinaryMessage binaryMessage;
        binaryMessage.SetMessage(messageData);
        PublishMessage publishMessage;
        publishMessage.SetBinaryMessage(binaryMessage);
        request.SetTopic(topic);
        request.SetPublishMessage(publishMessage);

        auto operation = ipcClient.NewPublishToTopic();
        auto activate = operation->Activate(request, nullptr);
        activate.wait();

        auto responseFuture = operation->GetResult();
        if(responseFuture.wait_for(std::chrono::seconds(timeout)) == std::future_status::timeout) {
            std::cerr << "Operation timed out while waiting for response from Greengrass Core."
                      << std::endl;
            exit(-1);
        }

        auto response = responseFuture.get();
        if(!response) {
            // Handle error.
            auto errorType = response.GetResultType();
            if(errorType == OPERATION_ERROR) {
                auto *error = response.GetOperationError();
                (void) error;
                // Handle operation error.
            } else {
                // Handle RPC error.
            }
        }
    }
    {
        QOS qos = QOS_AT_MOST_ONCE;
        PublishToIoTCoreRequest request;
        Vector<uint8_t> messageData({message.begin(), message.end()});
        request.SetTopicName(topic);
        request.SetPayload(messageData);
        request.SetQos(qos);

        auto operation = ipcClient.NewPublishToIoTCore();
        auto activate = operation->Activate(request, nullptr);
        activate.wait();

        auto responseFuture = operation->GetResult();
        if(responseFuture.wait_for(std::chrono::seconds(timeout)) == std::future_status::timeout) {
            std::cerr << "Operation timed out while waiting for response from Greengrass Core."
                      << std::endl;
            exit(-1);
        }

        auto response = responseFuture.get();
        if(!response) {
            // Handle error.
            auto errorType = response.GetResultType();
            if(errorType == OPERATION_ERROR) {
                auto *error = response.GetOperationError();
                (void) error;
                // Handle operation error.
            } else {
                // Handle RPC error.
            }
        }
    }
    return 0;
}
