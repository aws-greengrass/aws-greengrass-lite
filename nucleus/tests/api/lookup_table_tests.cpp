#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <lookup_table.hpp>
// NOLINTBEGIN
SCENARIO("LookupTable", "[lookup]") {
    GIVEN("A compile-time generated lookup table") {
        constexpr util::LookupTable table{1, 2.0, 2, 3.0, 3, 4.0};

        THEN("Schema is correct") {
            STATIC_REQUIRE(table.size() == 3);
            STATIC_REQUIRE(table.max_size() == 3);
            STATIC_REQUIRE(std::is_same_v<decltype(table)::source_type, int>);
            STATIC_REQUIRE(std::is_same_v<decltype(table)::mapped_type, double>);
        }

        THEN("Layout is correct") {
            STATIC_REQUIRE(table.get<0>() == std::pair{1, 2.0});
            STATIC_REQUIRE(table.get<1>() == std::pair{2, 3.0});
            STATIC_REQUIRE(table.get<2>() == std::pair{3, 4.0});
        }

        WHEN("Looking up a missing key") {
            constexpr int key = 42;
            constexpr std::optional<double> value = table.lookup(key);
            THEN("Lookup is valueless") {
                STATIC_REQUIRE(value == std::nullopt);
            }
        }

        WHEN("Looking up a key") {
            int key = GENERATE(1, 2, 3);
            THEN("Index is correct") {
                std::optional<uint32_t> idx = table.indexOf(key);
                REQUIRE(idx == (key - 1));
            }
            THEN("Value is correct") {
                std::optional<double> value = table.lookup(key);
                REQUIRE(value == (1.0 + key));
                AND_THEN("Reverse lookup succeeds") {
                    std::optional<int> rkey = table.rlookup(value.value());
                    REQUIRE(rkey == key);
                }
            }
        }

        WHEN("Looking up a value") {
            double value = GENERATE(2.0, 3.0, 4.0);
            THEN("Index is correct") {
                std::optional<uint32_t> idx = table.rindexOf(value);
                REQUIRE(idx == static_cast<uint32_t>(value - 2.0));
            }
            THEN("Value is correct") {
                std::optional<int> key = table.rlookup(value);
                REQUIRE(key == static_cast<int>(value - 1.0));
                AND_THEN("Reverse lookup succeeds") {
                    std::optional<double> rvalue = table.lookup(key.value());
                    REQUIRE(rvalue == value);
                }
            }
        }

        WHEN("Looking up a missing value") {
            constexpr double value = 42.0;
            constexpr std::optional<int> key = table.rlookup(value);
            THEN("Lookup is valueless") {
                STATIC_REQUIRE(key == std::nullopt);
            }
        }
    }
}
// NOLINTEND
