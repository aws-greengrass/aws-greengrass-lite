#include "deployment/model/linked_map.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <unordered_set>

namespace util {
    template<class Container, class Pred>
    void erase_if(Container &c, Pred pred) {
        auto first = c.cbegin();
        while(first != c.cend()) {
            if(pred(*first)) {
                first = c.erase(first);
            } else {
                ++first;
            }
        }
    }

    template<class K, class V, class Range>
    bool contains_all(const data::LinkedMap<K, V> &map, const Range &range) {
        return std::all_of(std::cbegin(range), std::cend(range), [&map](const K &key) {
            return map.contains(key);
        });
    }

    class DependencyOrder {
    public:
        template<class K, class V, class DependencyGetterFn>
        std::unique_ptr<data::LinkedMap<K, V>> computeOrderedDependencies(
            std::unordered_map<K, V> &pendingDependencies, DependencyGetterFn &&dependencyGetter) {
            auto dependencyFound = std::make_unique<data::LinkedMap<K, V>>();
            computeOrderedDependencies(
                *dependencyFound,
                pendingDependencies,
                std::forward<DependencyGetterFn>(dependencyGetter));
            return dependencyFound;
        }

        template<class K, class V, class DependencyGetterFn>
        void computeOrderedDependencies(
            data::LinkedMap<K, V> &dependencyFound,
            std::unordered_map<K, V> &pendingDependencies,
            DependencyGetterFn &&dependencyGetter) {
            while(!pendingDependencies.empty()) {
                auto sz = pendingDependencies.size();
                erase_if(pendingDependencies, [&](auto &&pending) {
                    if(contains_all(dependencyFound, dependencyGetter(pending.second))) {
                        dependencyFound.push(pending);
                        return true;
                    }
                    return false;
                });
                if(sz == pendingDependencies.size()) {
                    // didn't find anything to remove, there must be a cycle or a missing component
                    break;
                }
            }
        }
    };

} // namespace util
