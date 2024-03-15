#pragma once
#include "tracked_object.hpp"
#include <list>
#include <optional>
#include <shared_mutex>

namespace data {
    template<class K, class V>
    class SharedLinkedMap : public TrackedObject {
        std::list<std::pair<K, V>> _pairList;
        std::unordered_map<K, typename decltype(_pairList)::const_iterator> _hashMap;
        mutable std::shared_mutex _mutex;

    public:
        explicit SharedLinkedMap(const scope::UsingContext &context) : TrackedObject(context) {
        }
        // Add elements to the queue in order
        void push(const std::pair<K, V> &arg)  {
            const auto &[key, value] = arg;
            // If the key doesn't exist, add it to the end of the map.
            if (!contains(key)) {
                std::unique_lock guard{_mutex};
                _pairList.push_back({key, value});
                auto itr = _pairList.cend();
                itr--;
                _hashMap.insert({key, itr});
            } else { // If key exists, replace the value and still maintain the order
                std::unique_lock guard{_mutex};
                auto itr = _hashMap.at(key);
                auto replace_itr = _pairList.insert(itr, {key, value}); // Inserts new value (same key) before the existing pair
                _hashMap.erase(key); // erase and insert the new itr as unordered map doesn't support replacing the key-value
                _hashMap.insert({key,replace_itr});
                _pairList.erase(itr); // Remove old value from the list
            }
        }

        // Returns and removes the first element from the queue
        [[nodiscard]] V poll() noexcept {
            if(isEmpty()) {
                return {}; //should return nothing
            }
            // Get value of the first element
            auto [key, value] = _pairList.front();
            std::unique_lock guard{_mutex};
            // removes first element from the list and the corresponding key from the map
            _pairList.pop_front();
            _hashMap.erase(key);
            return value;
        }

        [[nodiscard]] const V &get(const K &key) const noexcept {
            std::shared_lock lock{_mutex};
            auto it = _hashMap.at(key);
            return it->second;
        }

        [[nodiscard]] bool contains(const K &key) const noexcept {
            std::shared_lock lock(_mutex);
            return _hashMap.find(key) != _hashMap.cend();
        }

        [[nodiscard]] bool isEmpty() const noexcept {
            std::shared_lock lock(_mutex);
            return _pairList.empty();
        }

        void remove(const K &key) noexcept {
            if(!contains(key)) {
                return;
            }
            auto iter = _hashMap.at(key);
            std::unique_lock guard{_mutex};
            _pairList.erase(iter);
            _hashMap.erase(key);
        }

        long size() noexcept {
            std::shared_lock lock{_mutex};
            return _hashMap.size();
        }

        void clear() noexcept {
            std::unique_lock guard{_mutex};
            _pairList.clear();
            _hashMap.clear();
        }
    };
} // namespace data
