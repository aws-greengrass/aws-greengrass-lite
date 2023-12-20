#pragma once
#include "data/struct_model.hpp"
#include "data/tracked_object.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace tasks {
    class Callback;
}

namespace channel {
    //
    // A unidirectional stream of GG Structs with shared ownership
    //
    class Channel final : public data::TrackedObject {
    private:
        std::queue<std::shared_ptr<data::StructModelBase>> _inFlight;
        std::shared_ptr<tasks::Callback> _listener;
        std::vector<std::shared_ptr<tasks::Callback>> _onClose;
        mutable std::mutex _mutex;
        std::thread _worker;
        std::condition_variable _wait;
        std::atomic_bool _workerStarted{};
        std::atomic_bool _terminate{};
        std::atomic_bool _closed{};

        void channelWorker();

    public:
        explicit Channel(const std::shared_ptr<scope::Context> &context) : TrackedObject{context} {
        }

        ~Channel() noexcept override;

        void write(const std::shared_ptr<data::StructModelBase> &value);
        void close();
        void setListenCallback(const std::shared_ptr<tasks::Callback> &callback);
        void setCloseCallback(const std::shared_ptr<tasks::Callback> &callback);
    };
} // namespace channel
