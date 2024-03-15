
#include "deployment/model/dependency_order.hpp"
#include "deployment/model/linked_map.hpp"
#include <catch2/catch_all.hpp>
#include <map>
#include <string>
#include <unordered_map>

// NOLINTBEGIN
SCENARIO("Building a Dependency Order", "[deployment.model]") {
    GIVEN("An empty dependency list") {

        std::unordered_map<int, int> pending;

        WHEN("Computing the run order") {
            std::shared_ptr<data::LinkedMap<int, int>> runOrder =
                util::DependencyOrder{}.computeOrderedDependencies(
                    pending, [](int) -> std::array<int, 0> { return {}; });

            THEN("An empty run order is created") {
                REQUIRE(static_cast<bool>(runOrder));
                REQUIRE(runOrder->empty());
            }

            THEN("Pending map is unchanged") {
                REQUIRE(pending.empty());
            }
        }
    }

    GIVEN("A simple dependency list") {
        using namespace std::literals;
        // 4 -> 1 -> 2 -> 3
        // run order: 3,2,1,4
        std::map<std::string, std::unordered_set<int>, std::less<>> dependencyLookup{
            {"4"s, {1}}, {"1"s, {2}}, {"2"s, {3}}, {"3"s, {}}};
        std::unordered_map<int, std::string> pending{{1, "1"s}, {2, "2"s}, {3, "3"s}, {4, "4"s}};

        WHEN("Computing the run order") {
            std::shared_ptr<data::LinkedMap<int, std::string>> runOrder =
                util::DependencyOrder{}.computeOrderedDependencies(
                    pending, [&dependencyLookup](std::string_view key) -> std::unordered_set<int> {
                        auto iter = dependencyLookup.find(key);
                        if(iter == dependencyLookup.end()) {
                            return {};
                        }
                        return iter->second;
                    });

            THEN("All pending elements are processed") {
                REQUIRE(pending.empty());
                REQUIRE(static_cast<bool>(runOrder));
            }

            THEN("The run order is correct") {
                REQUIRE(runOrder->size() == 4);
                REQUIRE(runOrder->poll() == "3"s);
                REQUIRE(runOrder->poll() == "2"s);
                REQUIRE(runOrder->poll() == "1"s);
                REQUIRE(runOrder->poll() == "4"s);
            }
        }
    }

    GIVEN("A complex dependency graph") {
        using namespace std::literals;
        /*
        //      1     7
        //     / \    |
        //    4   5   |
        //     \ / \ /
        //      3   6
        */
        // Possible run order: 1,7,5,6,4,3

        std::map<std::string, std::unordered_set<int>, std::less<>> dependencyLookup{
            {"1"s, {}}, {"7"s, {}}, {"5"s, {1}}, {"6"s, {5, 7}}, {"4"s, {1}}, {"3"s, {4, 5}}};
        std::set<std::string, std::less<>> alreadyRan{};
        std::unordered_map<int, std::string> pending{
            {1, "1"s}, {3, "3"s}, {4, "4"s}, {5, "5"s}, {6, "6"s}, {7, "7"s}};
        auto pendingCopy = pending;
        WHEN("Computing the run order") {
            std::shared_ptr<data::LinkedMap<int, std::string>> runOrder =
                util::DependencyOrder{}.computeOrderedDependencies(
                    pending, [&dependencyLookup](std::string_view key) -> std::unordered_set<int> {
                        auto iter = dependencyLookup.find(key);
                        if(iter == dependencyLookup.end()) {
                            return {};
                        }
                        return iter->second;
                    });

            THEN("All pending elements are processed") {
                REQUIRE(pending.empty());
                REQUIRE(static_cast<bool>(runOrder));
            }

            THEN("The run order is valid") {
                REQUIRE(runOrder->size() == 6);
                while(!runOrder->empty()) {
                    auto top = runOrder->poll();
                    auto deps = dependencyLookup.find(top);
                    REQUIRE(deps != dependencyLookup.cend());
                    for(auto &dependency : deps->second) {
                        REQUIRE(alreadyRan.count(pendingCopy.at(dependency)) == 1);
                    }
                    // each component is run exactly once
                    REQUIRE(alreadyRan.emplace(top).second);
                }

                for(const auto &[key, value] : pendingCopy) {
                    REQUIRE(alreadyRan.count(value) == 1);
                }
            }
        }
    }

    GIVEN("A circular dependency") {
        using namespace std::literals;

        std::map<std::string, std::unordered_set<int>, std::less<>> dependencyLookup{
            {"1"s, {2}}, {"2"s, {1}}, {"3"s, {}}};
        std::unordered_map<int, std::string> pending{{1, "1"s}, {2, "2"s}, {3, "3"s}};

        WHEN("Computing the run order") {
            std::shared_ptr<data::LinkedMap<int, std::string>> runOrder =
                util::DependencyOrder{}.computeOrderedDependencies(
                    pending, [&dependencyLookup](std::string_view key) -> std::unordered_set<int> {
                        auto iter = dependencyLookup.find(key);
                        if(iter == dependencyLookup.end()) {
                            return {};
                        }
                        return iter->second;
                    });

            THEN("Unsatisfied dependencies remain in the pending map") {
                REQUIRE(pending.size() == 2);
                REQUIRE(pending.count(1) == 1);
                REQUIRE(pending.count(2) == 1);
            }

            THEN("Satisfied dependencies are in run order") {
                REQUIRE(static_cast<bool>(runOrder));
                REQUIRE(runOrder->size() == 1);
                REQUIRE(runOrder->poll() == "3"s);
            }
        }
    }

    GIVEN("A harder to detect circular dependency") {
        using namespace std::literals;
        // 4 -> 1 -> 2 -> 3 -> 4 | 6 -> 5
        // Circular dependency between 3 and 4
        std::map<std::string, std::unordered_set<int>, std::less<>> dependencyLookup{
            {"4"s, {1}}, {"1"s, {2}}, {"2"s, {3}}, {"3"s, {4}}, {"6"s, {5}}, {"5"s, {}}};
        std::unordered_map<int, std::string> pending{
            {1, "1"s}, {2, "2"s}, {3, "3"s}, {4, "4"s}, {5, "5"s}, {6, "6"s}};

        WHEN("Computing the run order") {
            std::shared_ptr<data::LinkedMap<int, std::string>> runOrder =
                util::DependencyOrder{}.computeOrderedDependencies(
                    pending, [&dependencyLookup](std::string_view key) -> std::unordered_set<int> {
                        auto iter = dependencyLookup.find(key);
                        if(iter == dependencyLookup.end()) {
                            return {};
                        }
                        return iter->second;
                    });

            THEN("Unsatisfied dependencies remain in the pending map") {
                REQUIRE(pending.size() == 4);
                REQUIRE(pending.count(1) == 1);
                REQUIRE(pending.count(2) == 1);
                REQUIRE(pending.count(3) == 1);
                REQUIRE(pending.count(4) == 1);
            }

            THEN("Satisfied dependencies are in the correct run order") {
                REQUIRE(runOrder->size() == 2);
                REQUIRE(runOrder->poll() == "5"s);
                REQUIRE(runOrder->poll() == "6"s);
            }
        }
    }
}
// NOLINTEND
