#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>

// NOLINTBEGIN
SCENARIO("Shared list API", "[list]") {
    auto threadScope = ggapi::ThreadScope::claimThread();
    auto callScope = ggapi::CallScope::newCallScope();

    GIVEN("A list") {
        auto list = ggapi::List::create();

        THEN("List is empty") {
            REQUIRE(list.size() == 0);
        }
        WHEN("Items are appended to list") {
            list.append({uint64_t{1}, uint64_t{2}, "foo", 4.6, true});
            THEN("List size increased") {
                REQUIRE(list.size() == 5);
            }
            THEN("List contents are as expected") {
                REQUIRE(list.get<int>(0) == 1);
                REQUIRE(list.get<int>(1) == 2);
                REQUIRE(list.get<std::string>(2) == "foo");
                REQUIRE(list.get<float>(3) == 4.6F);
                REQUIRE(list.get<std::string>(4) == "true");
            }
            AND_WHEN("Items are put into list") {
                list.put(2, "zing").put(3, 5).put(-1, false).put(0, 9);
                THEN("List size remains the same") {
                    REQUIRE(list.size() == 5);
                }
                THEN("List contents are as expected") {
                    REQUIRE(list.get<int>(0) == 9);
                    REQUIRE(list.get<int>(1) == 2);
                    REQUIRE(list.get<std::string>(2) == "zing");
                    REQUIRE(list.get<int>(3) == 5);
                    REQUIRE(list.get<std::string>(4) == "false");
                }
            }
            AND_WHEN("Items are inserted into list") {
                list.insert(2, "x").insert(0, "y").insert(-2, "z");
                THEN("List size increases") {
                    REQUIRE(list.size() == 8);
                }
                THEN("List contents are as expected") {
                    REQUIRE(list.get<std::string>(0) == "y");
                    REQUIRE(list.get<int>(1) == 1);
                    REQUIRE(list.get<int>(2) == 2);
                    REQUIRE(list.get<std::string>(3) == "x");
                    REQUIRE(list.get<std::string>(4) == "foo");
                    REQUIRE(list.get<double>(5) == 4.6);
                    REQUIRE(list.get<std::string>(6) == "z");
                    REQUIRE(list.get<bool>(7) == true);
                }
            }
        }
    }
}

// NOLINTEND
