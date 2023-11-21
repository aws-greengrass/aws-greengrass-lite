#include "IPCSocket.hpp"

#include <condition_variable>
#include <cstddef>

#include <algorithm>
#include <exception>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <ratio>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>

#include <cpp_api.hpp>

struct Keys {
    ggapi::StringOrd start{"start"};
    ggapi::StringOrd run{"run"};
    ggapi::StringOrd shutdown{"shutdown"};
    ggapi::StringOrd publishToIoTCoreTopic{"aws.greengrass."
                                           "PublishToIoTCore"};
    ggapi::StringOrd topicName{"topicName"};
    ggapi::StringOrd qos{"qos"};
    ggapi::StringOrd payload{"payload"};
    ggapi::StringOrd retain{"retain"};
    ggapi::StringOrd userProperties{"userProperties"};
    ggapi::StringOrd messageExpiryIntervalSeconds{"messageExpiryIntervalSecon"
                                                  "ds"};
    ggapi::StringOrd correlationData{"correlationData"};
    ggapi::StringOrd responseTopic{"responseTopic"};
    ggapi::StringOrd payloadFormat{"payloadFormat"};
    ggapi::StringOrd contentType{"contentType"};

    static const Keys &get() {
        static Keys keys{};
        return keys;
    }
};

namespace Aws {
    namespace Greengrass {
        class IpcThread {
            std::atomic<bool> _running{};
            std::vector<CloseableSocket> _clients;
            DomainSocket<Server> _listen;
            std::condition_variable barrier;
            std::mutex m;

        public:
            explicit IpcThread(std::filesystem::path path) : _listen(std::move(path)) {
            }

            bool signalStart() noexcept {
                if(!_running.exchange(true)) {
                    barrier.notify_one();
                    return true;
                }
                return false;
            }

            bool signalStop() noexcept {
                return _running.exchange(false);
            }

            void operator()() {
                // wait for doRunPhase
                {
                    std::unique_lock lock{m};
                    barrier.wait(lock);
                }

                SocketSet set{_listen};
                while(_running.load()) {
                    using namespace std::chrono_literals;
                    std::error_code ec{};

                    // wait for next socket event, need to poll running status every second
                    auto workingSet = set.select(1s, ec);
                    if(ec) {
                        std::cerr << IPC_LOG_TAG << "ERROR: " << ec << '\n';
                        throw std::system_error(ec);
                    } else if(workingSet.empty()) {
                        continue;
                    }

                    bool clientClosed = false;
                    auto ready = workingSet.size();
                    for(auto &client : _clients) {
                        if(ready == 0) {
                            break;
                        } else if(!workingSet.contains(client)) {
                            continue;
                        }

                        --ready;

                        static constexpr size_t bufferLen = 1024;
                        std::array<char, bufferLen> buffer{};
                        auto count = client.read(buffer, ec);
                        if(count == 0) {
                            if(ec) {
                                std::cerr << IPC_LOG_TAG << "ERROR:"
                                          << "recv failed: " << ec << '\n';
                            }

                            std::cout << IPC_LOG_TAG << " INFO:"
                                      << "Removing socket " << client.getSocket() << '\n';
                            set.erase(client);
                            clientClosed = true;
                        } else {
                            // TODO: IPC streaming
                            // TODO: topic subscription
                            auto toSend = std::string_view{buffer.data(), count};
                            auto &keys = Keys::get();
                            std::cout << IPC_LOG_TAG << "INFO: Sending..." << std::endl;
                            ggapi::Struct result = ggapi::Task::sendToTopic(
                                keys.publishToIoTCoreTopic,
                                ggapi::Struct::create()
                                    .put(keys.topicName, std::to_string(client.getSocket()))
                                    .put(keys.qos, 1)
                                    .put(keys.payload, toSend));
                            std::cout << IPC_LOG_TAG << "INFO: Sending complete." << '\n'
                                      << toSend << std::endl;
                        }
                    }

                    // close removed sockets
                    if(clientClosed) {
                        _clients.erase(
                            std::remove_if(
                                _clients.begin(),
                                _clients.end(),
                                [&set](auto &client) { return !set.contains(client); }),
                            _clients.end());
                    }

                    // TODO: forward subscription messages to clients

                    // accept new sockets from listener
                    if(workingSet.contains(_listen)) {
                        auto size = _clients.size();
                        _listen.accept_range(std::back_inserter(_clients), set, ec);
                        if(ec) {
                            std::cerr << IPC_LOG_TAG << "ERROR: Accept failed: " << ec << "\n";
                            throw std::system_error(ec);
                        }
                    }
                }
            }
        };
    }; // namespace Greengrass
}; // namespace Aws

auto doStartPhase() {
    std::filesystem::path socketPath{"file"};
    auto t = std::make_unique<Aws::Greengrass::IpcThread>(socketPath);
    auto thread = std::thread(&Aws::Greengrass::IpcThread::operator(), t.get());
    return std::make_pair(std::move(t), std::move(thread));
}

void doRunPhase() {
    std::filesystem::path socketPath{"file"};

    static constexpr size_t clientN = 5;
    std::array<std::thread, clientN> clients;
    std::generate(
        clients.begin(), clients.end(), [&socketPath, count = 0]() mutable -> std::thread {
            return std::thread{
                [&socketPath](int count) {
                    DomainSocket<Client> client{socketPath};
                    for(size_t i = 0; i != count; ++i) {
                        static constexpr std::string_view message = "hello world";
                        std::stringstream ss;
                        ss << message << " from " << std::this_thread::get_id() << '\n';
                        auto str = ss.str();
                        auto buffer = util::Span{str.data(), str.size()};
                        std::error_code ec{};
                        std::cout << "Client starts write\n";
                        auto count = client.write(buffer, ec);

                        if(ec) {
                            std::cerr << "Client couldn't write\n";
                            return;
                        } else {
                            std::cout << "Client finishes write\n";
                        }

                        using namespace std::literals;
                        std::this_thread::sleep_for(10us);
                    }
                },
                ++count * 4};
        });

    for(auto &client : clients) {
        client.join();
    }
}

extern "C" bool greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept
    try {
    std::cout << "Running lifecycle plugins 1... " << ggapi::StringOrd{phase}.toString()
              << std::endl;
    const auto &keys = Keys::get();

    ggapi::StringOrd phaseOrd{phase};

    static std::unique_ptr<Aws::Greengrass::IpcThread> ipc{};
    static std::thread thread{};

    if(phaseOrd == keys.start) {
        if(!ipc && !thread.joinable()) {
            std::tie(ipc, thread) = doStartPhase();
        }
    } else if(phaseOrd == keys.run) {
        if(ipc && thread.joinable()) {
            if(ipc->signalStart()) {
                doRunPhase();
            }
        }
    } else if(phaseOrd == keys.shutdown) {
        if(ipc && thread.joinable()) {
            ipc->signalStop();
            thread.join();
        }
        ipc.reset();
    }

    return true;
} catch(const std::exception &e) {
    std::cerr << e.what() << '\n';
    return false;
} catch(...) {
    std::cerr << "Unknown exception caught\n";
    return false;
}
