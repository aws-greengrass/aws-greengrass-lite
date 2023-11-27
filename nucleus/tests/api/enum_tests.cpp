#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>
#include <util.hpp>

// NOLINTBEGIN

SCENARIO("Test enum capabilities", "[enum]") {
    GIVEN("An enum") {
        ggapi::Symbol foo("foo");
        ggapi::Symbol bar("bar");
        ggapi::Symbol baz("baz");
        enum class MyEnum { Foo, Bar, Baz, Other };
        util::LookupTable table{foo, MyEnum::Foo, bar, MyEnum::Bar, baz, MyEnum::Baz};
        WHEN("Performing a lookup") {
            ggapi::Symbol value("bar");
            MyEnum val = table.lookup(value).value_or(MyEnum::Other);
            THEN("The correct value is returned") {
                REQUIRE(val == MyEnum::Bar);
            }
        }
        WHEN("Performing a reverse lookup") {
            ggapi::Symbol sym = table.rlookup(MyEnum::Baz).value_or(ggapi::Symbol{});
            THEN("The correct value is returned") {
                REQUIRE(sym == baz);
            }
        }
        WHEN("Performing looking up a value that doesn't exist") {
            auto val = table.lookup(ggapi::Symbol("missing"));
            THEN("No value was returned") {
                REQUIRE_FALSE(val.has_value());
            }
        }
    }
}

// NOLINTEND
