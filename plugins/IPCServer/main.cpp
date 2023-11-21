#include "IPCSocket.hpp"

#include <cstddef>

#include <algorithm>
#include <exception>
#include <iostream>
#include <iterator>
#include <ratio>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>

#include <cpp_api.hpp>

struct Keys {
    ggapi::StringOrd start{"start"};
    ggapi::StringOrd run{"run"};
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

        public:
            explicit IpcThread(std::filesystem::path path) : _listen(std::move(path)) {
            }

            void operator()() {
                _running.store(true);
                SocketSet set{_listen};
                while(_running.load()) {
                    using namespace std::chrono_literals;
                    std::error_code ec{};

                    // wait for next socket event
                    auto workingSet = set.select(30s, ec);
                    if(ec) {
                        std::cerr << IPC_LOG_TAG << "ERROR: " << ec << '\n';
                        throw std::runtime_error("Socket");
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
                            auto sent = std::string_view{buffer.data(), count};
                            auto request{ggapi::Struct::create()};
                            auto &keys = Keys::get();
                            request.put(keys.topicName, std::to_string(client.getSocket()));
                            request.put(keys.qos, 1);
                            request.put(keys.payload, sent);

                            std::cout << IPC_LOG_TAG << "INFO: Sending..." << std::endl;
                            ggapi::Struct result =
                                ggapi::Task::sendToTopic(keys.publishToIoTCoreTopic, request);
                            std::cout << IPC_LOG_TAG << "INFO: Sending complete." << '\n'
                                      << sent << std::endl;
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

                    // accept new sockets from listener
                    if(workingSet.contains(_listen)) {
                        auto size = _clients.size();
                        _listen.accept(std::back_inserter(_clients), set, ec);
                        if(ec) {
                            std::cerr << IPC_LOG_TAG << "ERROR: Accept failed: " << ec << "\n";
                            throw std::system_error(ec);
                        }
                    }
                }
            }

            bool signalStop() noexcept {
                return _running.exchange(false);
            }
        };

        void entry() {
            std::filesystem::path socketPath{"file"};
            IpcThread t(socketPath);
            std::thread thread(&IpcThread::operator(), &t);

            using namespace std::literals;
            std::this_thread::sleep_for(1s);

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

                                std::this_thread::sleep_for(10us);
                            }
                        },
                        ++count * 4};
                });

            for(auto &client : clients) {
                client.join();
            }

            t.signalStop();
            thread.join();

            std::cout << "Server done\n";
        }

    }; // namespace Greengrass
}; // namespace Aws

void doStartPhase() {
}

void doRunPhase() {
    Aws::Greengrass::entry();
}

extern "C" bool greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept
    try {
    std::cout << "Running lifecycle plugins 1... " << ggapi::StringOrd{phase}.toString()
              << std::endl;
    const auto &keys = Keys::get();

    ggapi::StringOrd phaseOrd{phase};
    if(phaseOrd == keys.start) {
        doStartPhase();
    } else if(phaseOrd == keys.run) {
        doRunPhase();
    }

    return true;
} catch(const std::exception &e) {
    std::cerr << e.what() << '\n';
    return false;
} catch(...) {
    std::cerr << "Unknown exception caught\n";
    return false;
}
