#include "syscall.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <exception>
#include <futures.hpp>
#include <gg_pal/file_descriptor.hpp>
#include <gg_pal/process.hpp>
#include <gg_pal/process_manager.hpp>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <system_error>
#include <thread>
#include <type_traits>
#include <typeinfo>
#include <unistd.h>
#include <variant>

namespace ipc {

    namespace {
        using namespace std::chrono_literals;
        constexpr auto epollTimeout = 1000ms;
        constexpr int maxEvents = 10;
        constexpr auto minTimePoint = std::chrono::steady_clock::time_point::min();

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
        int addEpollEvent(FileDescriptor &epollFd, FileDescriptor &fd, uint32_t events, T &data) {
            epoll_event e{};
            e.events = events;
            e.data.ptr = &data;
            return epoll_ctl(epollFd.get(), EPOLL_CTL_ADD, fd.get(), &e);
        }

        int deleteEpollEvent(FileDescriptor &epollFd, FileDescriptor &fd) {
            struct epoll_event e {};
            return epoll_ctl(epollFd.get(), EPOLL_CTL_DEL, fd.get(), &e);
        }
    } // namespace

    ProcessId::operator bool() const noexcept {
        return pidfd >= 0;
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
        int epollFd = epoll_create1(EPOLL_CLOEXEC);
        if(epollFd == -1) {
            perror("epoll_create");
            throw std::system_error(errno, std::generic_category());
        }
        return FileDescriptor{epollFd};
    }

    void LinuxProcessManager::addEvent(std::list<ProcessEvent> &eventList, ProcessEvent event) {
        eventList.emplace_front(std::move(event));
        auto [events, fd] = std::visit(
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
        if(addEpollEvent(_epollFd, fd, events, eventList.front()) < 0) {
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
                auto n = epoll_wait(_epollFd.get(), events.data(), maxEvents, epollTimeout.count());
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
                                    if(deleteEpollEvent(_epollFd, fd) < 0) {
                                        perror("epoll_ctl");
                                    }
                                    fd.close();
                                }
                            } else if constexpr(std::is_same_v<EventT, ProcessComplete>) {
                                auto &fd = e.process->getProcessFd();
                                if(deleteEpollEvent(_epollFd, fd) < 0) {
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
                      << "Unexpected exception\n";
        }
        _running.store(false);
    }

    ProcessId LinuxProcessManager::registerProcess(std::unique_ptr<Process> p) {
        if(!p || !p->isRunning()) {
            return {-1, -1};
        }

        if(!_running.load()) {
            throw std::runtime_error("Logger not running");
        }
        ProcessId pid{p->getPid(), p->getProcessFd().get()};

        if(auto timeoutPoint = p->getTimeout(); timeoutPoint != minTimePoint) {
            // TODO: Move timeout logic to lifecycle manager.
            // TODO: Fix miniscule time delay by doing conversion with timepoints. Keep timeout as
            // same unit throughout. (2s vs 1.9999s)
            auto currentTimePoint = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                timeoutPoint - currentTimePoint);
            auto delay = static_cast<uint32_t>(duration.count());

            ggapi::later(
                delay,
                [this](ProcessId pid) {
                    // TODO: Error out the child process via lifecycle manager.
                    std::ostringstream reasonStream;
                    reasonStream << "Process (pidfd=" << pid.pidfd
                                 << ") has reached the time out limit.";
                    try {
                        closeProcess(pid, reasonStream.str());
                    } catch(const std::system_error &ex) {
                        throw ex;
                    }
                },
                pid);
        }

        std::list<ProcessEvent> events;
        addEvent(events, ErrorLog{std::move(p->getErr()), std::move(p->getErrorHandler())});
        addEvent(events, OutLog{std::move(p->getOut()), std::move(p->getOutputHandler())});
        addEvent(events, ProcessComplete{std::move(p)});

        std::unique_lock guard{_listMutex};
        _fds.splice(_fds.begin(), std::move(events));
        guard.unlock();
        return pid;
    }

    void LinuxProcessManager::closeProcess(ProcessId id, std::string reason) {
        std::unique_lock guard{_listMutex};
        // TODO: ProcessId associative lookup
        auto found = std::find_if(_fds.begin(), _fds.end(), [&id](ProcessEvent &e) {
            return std::visit(
                [&id](const auto &e) -> bool {
                    using EventT = std::remove_cv_t<std::remove_reference_t<decltype(e)>>;
                    if constexpr(std::is_same_v<ProcessComplete, EventT>) {
                        return e.process->getPid() == id.pid;
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
        if(process.process->isRunning()) {
            // TODO: allow process to close gracefully
            std::cout << reason << std::endl;
            process.process->close(true);
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
