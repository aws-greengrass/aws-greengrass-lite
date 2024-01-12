#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN
SCENARIO("String ordinals are consistent", "[ordinal]") {

    GIVEN("A string table") {
        scope::LocalizedContext forTesting{scope::Context::create()};

        auto &symbols = scope::context()->symbols();
        WHEN("An ordinal is not assigned") {
            data::Symbol ord;
            THEN("Ordinal reports as null") {
                REQUIRE(ord.asInt() == 0);
                REQUIRE(ord.isNull());
            }
        }
        WHEN("A few ordinals are allocated") {
            auto foo{symbols.intern("foo")};
            auto bar{symbols.intern("bar")};
            auto baz{symbols.intern("baz")};
            THEN("The ordinals are non-null") {
                REQUIRE(foo.asInt() != 0);
                REQUIRE(bar.asInt() != 0);
                REQUIRE(baz.asInt() != 0);
            }
            THEN("The ordinal null assertValidSymbol is false") {
                REQUIRE(!foo.isNull());
            }
            THEN("The ordinals do not conflict") {
                REQUIRE(foo.asInt() != bar.asInt());
                REQUIRE(foo.asInt() != baz.asInt());
                REQUIRE(bar.asInt() != baz.asInt());
            }
            THEN("The ordinals return original strings") {
                REQUIRE(foo.toString() == "foo");
                REQUIRE(bar.toString() == "bar");
                REQUIRE(baz.toString() == "baz");
            }
            AND_WHEN("Duplicate ordinals are created") {
                auto foo2{symbols.intern("foo")};
                auto bar2{symbols.intern("bar")};
                auto bing{symbols.intern("bing")};
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
                auto foo2{symbols.intern("Foo")};
                auto bar2{symbols.intern("BAR")};
                THEN("Ordinals are unique") {
                    REQUIRE(foo.asInt() != foo2.asInt());
                    REQUIRE(bar.asInt() != bar2.asInt());
                }
            }
        }
        WHEN("Zero-length ordinal allocated") {
            auto empty{symbols.intern("")};
            THEN("The ordinals is non-null") {
                REQUIRE(empty.asInt() != 0);
            }
            THEN("Ordinal value is empty") {
                REQUIRE(empty.toString() == "");
            }
        }
    }
}

// NOLINTEND
