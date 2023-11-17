#include <cpp_api.hpp>
#include <iostream>

#include <filesystem>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <memory>
#include <string>

#include <chrono>
#include <sstream>
#include <thread>

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
        class Server {};

        class Client {};

        template<class S = Server>
        class DomainSocket {
        private:
            int _socketId;
            std::filesystem::path _path;
            static constexpr short GREENGRASS_IPC_PORT = 8383; // TODO: Configuration
            static constexpr int GREENGRASS_IPC_MAX_CONN = 32;

        public:
            fd_set initSet() const noexcept {
                fd_set set;
                FD_ZERO(&set);
                FD_SET(_socketId, &set);
                return set;
            }

            int getSocket() const noexcept {
                return _socketId;
            }

            DomainSocket(std::filesystem::path path) noexcept
                : _socketId(socket(AF_UNIX, SOCK_STREAM, 0)) {
                struct sockaddr_un name {};

                if(_socketId < 0) {
                    std::terminate();
                }

                static constexpr int on = 1;
                if(setsockopt(_socketId, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
                    std::terminate();
                }

                auto str = path.string();
                name.sun_family = AF_LOCAL;
                auto len = str.copy(name.sun_path, std::size(name.sun_path));
                if(len == std::size(name.sun_path)) {
                    *(std::end(name.sun_path) - 1) = '\0';
                    --len;
                }

                if constexpr(std::is_same_v<S, Server>) {
                    if(std::filesystem::exists(path)) {
                        std::error_code ec{};
                        std::filesystem::remove(path, ec);
                    }

                    if(bind(
                           _socketId,
                           reinterpret_cast<sockaddr *>(&name),
                           offsetof(sockaddr_un, sun_path) + len)
                       < 0) {
                        std::terminate();
                    }

                    if(listen(_socketId, GREENGRASS_IPC_MAX_CONN) < 0) {
                        std::terminate();
                    }

                    // make socket and all created sockets non-blocking
                    if(ioctl(_socketId, FIONBIO, &on) < 0) {
                        std::terminate();
                    }
                } else {
                    if(connect(
                           _socketId,
                           reinterpret_cast<sockaddr *>(&name),
                           offsetof(sockaddr_un, sun_path) + len)
                       < 0) {
                        std::terminate();
                    }
                }
            }

            ~DomainSocket() noexcept {
                if(_socketId > 0) {
                    if(close(_socketId) < 0) {
                        std::cout << "Couldn't close socket" << std::endl;
                    }
                }

                if constexpr(std::is_same_v<S, Server>) {
                    if(std::filesystem::exists(_path)) {
                        std::error_code ec{};
                        std::filesystem::remove(_path, ec);
                        if(ec != std::error_code{}) {
                            std::cout << "Couldn't break pipe " << ec.message() << '\n';
                        }
                    }
                }
            }
        };

        class IpcThread {
            std::atomic<bool> _running{};
            DomainSocket<Server> _listen;

        public:
            IpcThread(std::filesystem::path path) : _listen(path) {
            }

            void operator()() {
                _running.store(true);
                auto threadScope = ggapi::ThreadScope::claimThread();
                auto set = _listen.initSet();
                auto last = _listen.getSocket();
                while(_running.load()) {
                    timeval timeout{.tv_sec = 3, .tv_usec = 0};
                    auto workingSet = set;
                    int count = select(last + 1, &workingSet, nullptr, nullptr, &timeout);
                    if(count < 0) {
                        std::terminate();
                    }
                    if(count == 0) {
                        continue;
                    }
                    for(int i = 0, ready = count; i <= last && ready > 0; ++i) {
                        if(FD_ISSET(i, &workingSet)) {
                            ready--;
                            // new sockets ready from listener
                            if(i == _listen.getSocket()) {
                                int socket{};
                                do {
                                    socket = accept(_listen.getSocket(), nullptr, nullptr);
                                    if(socket < 0) {
                                        if(errno != EWOULDBLOCK) {
                                            std::cout << "Accept failed\n";
                                            _running.store(false);
                                        }
                                        break;
                                    }
                                    FD_SET(socket, &set);
                                    last = std::max(socket, last);
                                } while(socket != -1);
                            }
                            // data available from sockets
                            else {
                                std::array<char, 1028> buffer;
                                int count = recv(i, buffer.data(), buffer.size(), 0);
                                if(count < 0) {
                                    std::cout << "recv failed: " << errno << '\n';
                                } else if(count == 0) {
                                    std::cout << "Removing socket " << i << '\n';
                                    FD_CLR(i, &set);
                                    while(!FD_ISSET(last, &set)) {
                                        --last;
                                    }
                                } else {
                                    auto request{threadScope.createStruct()};
                                    auto &keys = Keys::get();
                                    request.put(keys.topicName, std::to_string(i));
                                    request.put(keys.qos, 1);
                                    request.put(
                                        keys.payload,
                                        std::string_view{
                                            buffer.data(), static_cast<size_t>(count)});

                                    std::cerr << "[example-ipc] Sending..." << std::endl;
                                    ggapi::Struct result = threadScope.sendToTopic(
                                        keys.publishToIoTCoreTopic, request);
                                    std::cerr << "[example-ipc] Sending complete." << std::endl;
                                    std::cout.write(buffer.data(), count) << '\n';
                                }
                            }
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

            std::array<std::thread, 5> clients;

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

                                if(int error = send(client.getSocket(), str.data(), str.size(), 0);
                                   error < 0) {
                                    std::terminate();
                                } else {
                                    std::cout << "Wrote " << error << " bytes\n";
                                }
                                std::this_thread::sleep_for(10us);
                            }
                        },
                        ++count * 100};
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

ggapi::Struct testListener(ggapi::Scope task, ggapi::StringOrd topic, ggapi::Struct callData) {
    std::string pingMessage{callData.get<std::string>("ping")};
    ggapi::Struct response = task.createStruct();
    response.put("pong", pingMessage);
    return response;
}

void doStartPhase() {
    //(void) ggapi::Scope::thisTask().subscribeToTopic(ggapi::StringOrd{"test"}, testListener);
    Aws::Greengrass::entry();
}

void doRunPhase() {
}

extern "C" bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
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
}
