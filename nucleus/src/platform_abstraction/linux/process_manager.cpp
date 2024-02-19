#include "process_manager.hpp"
#include "file_descriptor.hpp"
#include "process.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <exception>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <util.hpp>
#include <variant>

namespace ipc {

    namespace {
        using namespace std::chrono_literals;
        constexpr auto timeout = 1000ms;
        constexpr int maxEvents = 10;

        void raiseEventFd(FileDescriptor &eventfd, uint64_t count = 1) noexcept {
            if(-1 == eventfd.write(util::as_bytes(util::Span{&count, size_t{1}}))) {
                perror("write");
            }
        }

        uint64_t clearEventFd(FileDescriptor &eventfd) {
            uint64_t value{};
            if(-1 == eventfd.read(util::as_writeable_bytes(util::Span{&value, size_t{1}}))) {
                perror("read");
                return 0;
            }
            return value;
        }

        template<class T>
        int addEpollEvent(FileDescriptor &epfd, FileDescriptor &fd, uint32_t events, T &data) {
            epoll_event e{};
            e.events = events;
            e.data.ptr = &data;
            return epoll_ctl(epfd.get(), EPOLL_CTL_ADD, fd.get(), &e);
        }

        int deleteEpollEvent(FileDescriptor &epfd, FileDescriptor &fd) {
            struct epoll_event e {};
            return epoll_ctl(epfd.get(), EPOLL_CTL_DEL, fd.get(), &e);
        }
    } // namespace

    ProcessId::operator bool() const noexcept {
        return id >= 0;
    }

    FileDescriptor LinuxProcessManager::createEvent() {
        FileDescriptor event{eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
        if(!event) {
            perror("eventfd");
            throw std::system_error{errno, std::generic_category()};
        }
        return event;
    }

    FileDescriptor LinuxProcessManager::createEpoll() {
        int epfd = epoll_create1(EPOLL_CLOEXEC);
        if(epfd == -1) {
            perror("epoll_create");
            throw std::system_error(errno, std::generic_category());
        }
        return FileDescriptor{epfd};
    }

    void LinuxProcessManager::addEvent(std::list<ProcessEvent> &eventList, ProcessEvent event) {
        eventList.emplace_front(std::move(event));
        auto &&[events, fd] = std::visit(
            [&](auto &&e) -> std::pair<uint32_t, FileDescriptor &> {
                using EventT = std::remove_reference_t<decltype(e)>;
                if constexpr(std::is_same_v<EventT, InterruptEvent>) {
                    return {EPOLLIN, _eventfd};
                } else if constexpr(std::is_same_v<EventT, ProcessComplete>) {
                    return {EPOLLIN | EPOLLERR, e.process->getProcessFd()};
                } else {
                    return {EPOLLIN | EPOLLERR | EPOLLHUP, e.fd};
                }
            },
            eventList.front());
        if(addEpollEvent(_epfd, fd, events, eventList.front()) < 0) {
            perror("epoll_ctl");
            throw std::system_error{errno, std::generic_category()};
        }
    }

    void LinuxProcessManager::workerThread() noexcept {
        try {
            {
                std::unique_lock guard{_listMutex};
                addEvent(_fds, InterruptEvent{});
            }

            std::array<epoll_event, maxEvents> events{};
            while(_running) {
                auto n = epoll_wait(_epfd.get(), events.data(), maxEvents, timeout.count());
                if(n == -1) {
                    if(errno != EINTR) {
                        perror("epoll_wait");
                    }
                    continue;
                }
                std::for_each_n(events.begin(), n, [this](const epoll_event &event) {
                    std::visit(
                        [&](auto &&e) {
                            using EventT = std::remove_reference_t<decltype(e)>;
                            if constexpr(
                                std::is_same_v<EventT, ErrorLog>
                                || std::is_same_v<EventT, OutLog>) {
                                auto &&[fd, callback] = e;
                                if(event.events & EPOLLIN) {
                                    if(callback) {
                                        auto message = fd.readAll();
                                        callback(util::as_bytes(util::Span{message}));
                                    }
                                }
                                if(event.events & (EPOLLHUP | EPOLLERR)) {
                                    if(deleteEpollEvent(_epfd, fd) < 0) {
                                        perror("epoll_ctl");
                                    }
                                    fd.close();
                                }
                            } else if constexpr(std::is_same_v<EventT, ProcessComplete>) {
                                auto &fd = e.process->getProcessFd();
                                if(deleteEpollEvent(_epfd, fd) < 0) {
                                    perror("epoll_ctl");
                                }
                                std::error_code ec{};
                                auto returnCode = e.process->queryReturnCode(ec);
                                if(ec != std::error_code{}) {
                                    returnCode = -1;
                                    perror("waitid");
                                } else {
                                    std::cout << "Process (pidfd=" << fd.get()
                                              << ") closed with return code " << returnCode << '\n';
                                }
                                e.process->complete(returnCode);
                                fd.close();
                            } else if constexpr(std::is_same_v<EventT, InterruptEvent>) {
                                std::ignore = clearEventFd(_eventfd);
                            }
                        },
                        *static_cast<ProcessEvent *>(event.data.ptr));
                });

                std::unique_lock guard{_listMutex};
                // TODO: with something like boost::intrusive_list, this can be done in O(1) time as
                // the event which would remove it is raised
                _fds.remove_if([](auto &&e) {
                    return std::visit(
                        [](auto &&e) {
                            using EventT = std::remove_reference_t<decltype(e)>;
                            if constexpr(std::is_same_v<EventT, InterruptEvent>) {
                                return false;
                            } else if constexpr(std::is_same_v<EventT, ProcessComplete>) {
                                return !e.process->getProcessFd();
                            } else {
                                return !e.fd;
                            }
                        },
                        e);
                });
                guard.unlock();
            }
        } catch(std::exception &e) {
            std::cerr << "Linux Process Logger: " << e.what() << '\n';
        } catch(...) {
            std::cerr << "Linux Process Logger: "
                      << "Unexepected exception\n";
        }
        _running.store(false);
    }

    ProcessId LinuxProcessManager::registerProcess(std::unique_ptr<Process> p) {
        if(!p || !p->isRunning()) {
            return {-1};
        }

        if(!_running.load()) {
            throw std::runtime_error("Logger not running");
        }
        ProcessId pid{p->getProcessFd().get()};
        std::list<ProcessEvent> events;
        addEvent(events, ErrorLog{std::move(p->getErr()), std::move(p->getErrorHandler())});
        addEvent(events, OutLog{std::move(p->getOut()), std::move(p->getOutputHandler())});
        addEvent(events, ProcessComplete{std::move(p)});

        std::unique_lock guard{_listMutex};
        _fds.splice(_fds.begin(), std::move(events));
        guard.unlock();
        return pid;
    }

    void LinuxProcessManager::closeProcess(ProcessId id) {
        std::unique_lock guard{_listMutex};
        // TODO: ProcessId associative lookup
        auto found = std::find_if(_fds.begin(), _fds.end(), [&id](ProcessEvent &e) {
            return std::visit(
                [&id](const auto &e) {
                    using EventT = std::remove_reference_t<decltype(e)>;
                    if constexpr(std::is_same_v<EventT, ProcessComplete>) {
                        return e.process->getProcessFd().get() == id.id;
                    } else {
                        return false;
                    }
                },
                e);
        });

        if(found == _fds.end()) {
            return;
        }

        auto &process = std::get<ProcessComplete>(*found);
        // avoid a race condition where the process ends and its event handled after the event
        // listing is deleted
        if(deleteEpollEvent(_epfd, process.process->getProcessFd()) < 0) {
            perror("epoll_ctl");
        }
        if(process.process->isRunning()) {
            // TODO: allow process to close gracefully
            process.process->close(true);
            _fds.erase(found);
        }
    }

    LinuxProcessManager::~LinuxProcessManager() noexcept {
        if(!_running.exchange(false)) {
            return;
        }

        raiseEventFd(_eventfd);

        if(_thread.joinable()) {
            _thread.join();
        }
    }
} // namespace ipc
