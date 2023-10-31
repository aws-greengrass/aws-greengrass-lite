#include "data/globals.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN
SCENARIO("String ordinals are consistent", "[ordinal]") {

    GIVEN("A string table") {
        data::Environment environment;
        WHEN("An ordinal is not assigned") {
            data::StringOrd ord;
            THEN("Ordinal reports as null") {
                REQUIRE(ord.asInt() == 0);
                REQUIRE(ord.isNull());
            }
        }
        WHEN("A few ordinals are allocated") {
            auto foo{environment.stringTable.getOrCreateOrd("foo")};
            auto bar{environment.stringTable.getOrCreateOrd("bar")};
            auto baz{environment.stringTable.getOrCreateOrd("baz")};
            THEN("The ordinals are non-null") {
                REQUIRE(foo.asInt() != 0);
                REQUIRE(bar.asInt() != 0);
                REQUIRE(baz.asInt() != 0);
            }
            THEN("The ordinal null check is false") {
                REQUIRE(!foo.isNull());
            }
            THEN("The ordinals do not conflict") {
                REQUIRE(foo.asInt() != bar.asInt());
                REQUIRE(foo.asInt() != baz.asInt());
                REQUIRE(bar.asInt() != baz.asInt());
            }
            THEN("The ordinals return original strings") {
                REQUIRE(environment.stringTable.getString(foo) == "foo");
                REQUIRE(environment.stringTable.getString(bar) == "bar");
                REQUIRE(environment.stringTable.getString(baz) == "baz");
            }
            AND_WHEN("Duplicate ordinals are created") {
                auto foo2{environment.stringTable.getOrCreateOrd("foo")};
                auto bar2{environment.stringTable.getOrCreateOrd("bar")};
                auto bing{environment.stringTable.getOrCreateOrd("bing")};
                THEN("Duplicate ordinals are consistent") {
                    REQUIRE(foo.asInt() == foo2.asInt());
                    REQUIRE(bar.asInt() == bar2.asInt());
                }
                THEN("Non-duplicate ordinals are unique") {
                    REQUIRE(bing.asInt() != foo.asInt());
                    REQUIRE(bing.asInt() != bar.asInt());
                    REQUIRE(bing.asInt() != baz.asInt());
                }
            }
            AND_WHEN("Ordinals of mixed case are created") {
                auto foo2{environment.stringTable.getOrCreateOrd("Foo")};
                auto bar2{environment.stringTable.getOrCreateOrd("BAR")};
                THEN("Ordinals are unique") {
                    REQUIRE(foo.asInt() != foo2.asInt());
                    REQUIRE(bar.asInt() != bar2.asInt());
                }
            }
        }
    }
}

// NOLINTEND
