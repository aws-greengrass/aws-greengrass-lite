#include <condition_variable>
#include <mutex>
#include <queue>

template<typename T>
class ThreadSafeQueue {
    std::queue<T> _queue;
    std::mutex _mtx;
    std::condition_variable _cv;

public:
    void push(const T &value) {
        {
            std::unique_lock<std::mutex> lock{_mtx};
            _queue.push(value);
        }
        _cv.notify_one();
    }

    bool empty() {
        std::unique_lock<std::mutex> lock{_mtx};
        return _queue.empty();
    }

    T pop() {
        std::unique_lock<std::mutex> lock{_mtx};
        _cv.wait(lock, [this] { return !_queue.empty(); });

        T value = _queue.front();
        _queue.pop();

        return value;
    }
};
