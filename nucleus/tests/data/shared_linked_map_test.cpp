
#include "data/shared_linked_map.hpp"
#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN
SCENARIO("Operations on a linked map", "[data]") {
    GIVEN("A shared linked map") {
        scope::LocalizedContext forTesting{scope::Context::create()};

        WHEN("Push key-value pairs to the map") {
            auto linkedMap = data::SharedLinkedMap<std::string, std::string>(scope::context());
            linkedMap.push({"1", "first"});
            linkedMap.push({"2", "second"});
            linkedMap.push({"2", "updatedSecondValue"});
            linkedMap.push({"3", "third"});

            THEN("Map contains elements") {
                REQUIRE(!linkedMap.isEmpty());
                REQUIRE(linkedMap.size() == 3);
            }

            THEN("Get latest value for the key") {
                REQUIRE(linkedMap.get("1") == "first");
                REQUIRE(linkedMap.get("2") == "updatedSecondValue");
                REQUIRE(linkedMap.get("3") == "third");
            }

            THEN("Order of the elements is preserved") {
                REQUIRE(linkedMap.poll() == "first");
                REQUIRE(linkedMap.poll() == "updatedSecondValue");
                REQUIRE(linkedMap.poll() == "third");
            }
        }

        WHEN("Poll element from the map") {
            auto linkedMap = data::SharedLinkedMap<std::string, std::string>(scope::context());
            linkedMap.push({"1", "first"});
            linkedMap.push({"2", "second"});

            THEN("Return and remove the first element from the map") {
                REQUIRE(linkedMap.poll() == "first");
                REQUIRE(!linkedMap.contains("1"));
                REQUIRE(linkedMap.size() == 1);
            }
        }

        WHEN("Remove element by key from the map") {
            auto linkedMap = data::SharedLinkedMap<std::string, std::string>(scope::context());
            linkedMap.push({"1", "first"});
            linkedMap.push({"2", "second"});
            linkedMap.push({"3", "third"});
            linkedMap.push({"4", "four"});
            REQUIRE(linkedMap.contains("3"));
            linkedMap.remove("3");

            THEN("The key is removed from the map and the order is preserved") {
                REQUIRE(!linkedMap.contains("3"));
                REQUIRE(linkedMap.size() == 3);
                REQUIRE(linkedMap.poll() == "first");
                REQUIRE(linkedMap.poll() == "second");
                REQUIRE(linkedMap.poll() == "four");
            }
        }

        WHEN("Clear all elements from the map") {
            auto linkedMap = data::SharedLinkedMap<std::string, std::string>(scope::context());
            linkedMap.push({"1", "first"});
            linkedMap.push({"2", "second"});
            linkedMap.push({"3", "third"});
            linkedMap.push({"4", "four"});
            REQUIRE(linkedMap.size() == 4);
            linkedMap.clear();
            THEN("The map is empty") {
                REQUIRE(linkedMap.isEmpty());
                REQUIRE(linkedMap.size() == 0);
            }
        }
    }
}

// NOLINTEND
