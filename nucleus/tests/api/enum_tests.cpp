#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>
#include <util.hpp>

// NOLINTBEGIN

enum class MyEnum { Foo, Bar, Baz, Other };
using MyEnums = util::Enum<MyEnum, MyEnum::Foo, MyEnum::Bar, MyEnum::Baz>;

int func(MyEnums::ConstType<MyEnum::Foo>) {
    return 1;
}

int func(MyEnums::ConstType<MyEnum::Bar>) {
    return 2;
}

int func(MyEnums::ConstType<MyEnum::Baz>) {
    return 3;
}

SCENARIO("Test enum capabilities", "[enum]") {
    ggapi::Symbol foo("foo");
    ggapi::Symbol bar("bar");
    ggapi::Symbol baz("baz");
    GIVEN("An enum value") {
        util::LookupTable expected{MyEnum::Foo, 1, MyEnum::Bar, 2, MyEnum::Baz, 3};
        auto in = GENERATE(MyEnum::Foo, MyEnum::Bar, MyEnum::Baz);
        WHEN("Visiting the enum value") {
            std::optional<int> v = MyEnums::visit<int>(in, [](auto e) { return func(e); });
            THEN("Returned value is valid") {
                REQUIRE(v.has_value());
                REQUIRE(v.value() == expected.lookup(in).value_or(0));
            }
        }
    }
    GIVEN("An invalid enum value") {
        auto in = MyEnum::Other;
        WHEN("Visiting the enum value") {
            std::optional<int> v = MyEnums::visit<int>(in, [](auto e) { return func(e); });
            THEN("No value is returned") {
                REQUIRE_FALSE(v.has_value());
            }
        }
    }
    GIVEN("An enum table") {
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
