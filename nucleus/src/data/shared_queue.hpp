#pragma once
#include "tracked_object.hpp"
#include <list>
#include <optional>
#include <shared_mutex>

namespace data {
    template<class K, class V>
    class SharedQueue : public TrackedObject {
        std::list<std::pair<K, V>> _pairList;
        std::unordered_map<K, typename decltype(_pairList)::const_iterator> _hashMap;
        mutable std::shared_mutex _mutex;

    public:
        explicit SharedQueue(const scope::UsingContext &context) : TrackedObject(context) {
        }

        void push(const std::pair<K, V> &arg) noexcept {
            std::unique_lock guard{_mutex};
            const auto &[key, value] = arg;
            if(_hashMap.find(key) == _hashMap.cend()) {
                _pairList.emplace_front(key, value);
                _hashMap.insert({key, std::next(_pairList.cbegin())});
            }
        }

        [[nodiscard]] V get(const K &key) noexcept {
            std::unique_lock guard{_mutex};
            auto &it = _hashMap.at(key);
            return it->second;
        }

        [[nodiscard]] V &next() noexcept {
            std::unique_lock guard{_mutex};
            auto &kv = _pairList.front();
            return kv.second;
        }

        [[nodiscard]] bool exists(const K &key) noexcept {
            std::unique_lock guard{_mutex};
            return _hashMap.find(key) != _hashMap.cend();
        }

        [[nodiscard]] bool empty() noexcept {
            std::unique_lock guard{_mutex};
            return _pairList.empty();
        }

        void remove(const K &key) noexcept {
            if(exists(key)) {
                std::unique_lock guard{_mutex};
                auto iter = _hashMap.at(key);
                _pairList.erase(iter);
                _hashMap.erase(key);
            }
        }

        [[maybe_unused]] void pop() noexcept {
            std::unique_lock guard{_mutex};
            if(!_pairList.empty()) {
                auto &[key, value] = _pairList.front();
                _hashMap.erase(key);
                _pairList.pop_front();
            }
        }

        void clear() noexcept {
            _pairList.clear();
            _hashMap.clear();
        }
    };
} // namespace data
