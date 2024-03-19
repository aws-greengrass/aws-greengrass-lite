#pragma once
#include <unordered_map>
#include <list>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <iostream>

namespace data {
    template<class K, class V>
    class LinkedMap {
        using PairList = std::list<std::pair<K, V>>;
        PairList _pairList;
        std::unordered_map<K, typename PairList::iterator> _hashMap;
        mutable std::shared_mutex _mutex;

    public:
        // Add elements to the queue in order
        void push(const std::pair<K, V> &arg)  {
            const auto &[key, value] = arg;
            // If the key doesn't exist, add it to the end of the map.
            std::unique_lock guard{_mutex};
            if (auto foundMapIter = _hashMap.find(key); foundMapIter == _hashMap.end()) {
                _pairList.emplace_back(arg);
                auto itr = std::prev(_pairList.end());
                _hashMap.emplace(key, itr);
            } else { // If key exists, replace the value and still maintain the order
                const auto& listItr = foundMapIter->second;
                listItr->second = value; // replace old value in the list
            }
        }

        // Returns and removes the first element from the queue
        [[nodiscard]] V poll() noexcept {
            std::unique_lock guard{_mutex};
            if(_pairList.empty()) {
                return {}; //should return nothing
            }
            // Get value of the first element
            auto [key, value] = _pairList.front();
            // removes first element from the list and the corresponding key from the map
            _pairList.pop_front();
            _hashMap.erase(key);
            return value;
        }

        [[nodiscard]] V get(const K &key) const {
            std::shared_lock lock{_mutex};
            auto it = _hashMap.at(key);
            return it->second;
        }

        [[nodiscard]] bool contains(const K &key) const noexcept {
            std::shared_lock lock(_mutex);
            return _hashMap.find(key) != _hashMap.end();
        }

        [[nodiscard]] bool isEmpty() const noexcept {
            std::shared_lock lock(_mutex);
            return _pairList.empty();
        }

        void remove(const K &key) noexcept {
            std::unique_lock guard{_mutex};
            if(auto foundMapIter = _hashMap.find(key); foundMapIter != _hashMap.end()) {
                _pairList.erase(foundMapIter->second);
                _hashMap.erase(key);
            }
        }

        [[nodiscard]] long size() const noexcept {
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
