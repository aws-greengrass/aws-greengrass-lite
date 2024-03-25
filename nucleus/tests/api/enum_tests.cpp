#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <lookup_table.hpp>
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
    GIVEN("An enum value") {
        constexpr util::LookupTable expected{
            MyEnum::Foo,
            1,
            MyEnum::Bar,
            2,
            MyEnum::Baz,
            3,
        };

        WHEN("Visiting the enum value") {
            auto in = GENERATE(MyEnum::Foo, MyEnum::Bar, MyEnum::Baz);
            size_t callCount = 0;
            std::optional<int> v = MyEnums::visit<int>(in, [&callCount](auto e) {
                ++callCount;
                return func(e);
            });
            THEN("Returned value is valid") {
                REQUIRE(v.has_value());
                REQUIRE(v == expected.lookup(in));
                AND_THEN("Visitor is invoked exactly once") {
                    REQUIRE(callCount == 1);
                }
            }
        }
    }
    GIVEN("An invalid enum value") {
        auto in = MyEnum::Other;
        WHEN("Visiting the enum value") {
            size_t callCount = 0;
            std::optional<int> v = MyEnums::visit<int>(in, [&callCount](auto e) {
                ++callCount;
                return func(e);
            });
            THEN("No value is returned") {
                REQUIRE_FALSE(v.has_value());
                AND_THEN("Visitor is not invoked") {
                    REQUIRE(callCount == 0);
                }
            }
        }
    }
}

// NOLINTEND
