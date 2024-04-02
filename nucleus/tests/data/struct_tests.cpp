#include "data/shared_struct.hpp"
#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>
#include <memory>

// NOLINTBEGIN
SCENARIO("Shared structure implementation", "[struct]") {
    scope::LocalizedContext forTesting{};

    GIVEN("A structure") {
        auto s = std::make_shared<data::SharedStruct>(scope::context());
        data::Symbolish ping{"ping"};
        data::Symbolish pow{"pow"};

        // Overlaps with api/struct_tests
        THEN("Structure is empty") {
            REQUIRE(s->size() == 0);
        }
        // Overlaps with api/struct_tests
        WHEN("Items are added to structure key by key") {
            s->put("foo", 1);
            s->put(ping, 3);
            s->put("zing", 4.6);
            s->put("zap", "zoo");
            s->put(pow, pow);
            THEN("Structure size increased") {
                REQUIRE(s->size() == 5);
            }
            THEN("Structure contents are as expected") {
                REQUIRE(s->get("foo").getInt() == 1);
                REQUIRE(s->get("ping").getInt() == 3);
                REQUIRE(s->get("zing").getDouble() == 4.6);
                REQUIRE(s->get("zap").getString() == "zoo");
                REQUIRE(s->get("pow").getString() == "pow");
            }
        }
        // Augments api/struct_tests
        WHEN("Assigning a boxed value by string key") {
            auto b = std::make_shared<data::Boxed>(scope::context());
            b->put(5);
            s->put("boxed", b);
            THEN("Box contents was copied instead of box container") {
                auto v = s->get("boxed");
                REQUIRE_FALSE(v.isContainer()); // boxed value would appear as a container here
                REQUIRE(v.isScalar());
                REQUIRE(v.getInt() == 5);
            }
        }
        WHEN("Assigning a boxed value by symbol key") {
            auto b = std::make_shared<data::Boxed>(scope::context());
            b->put(5);
            s->put(pow, b);
            THEN("Box contents was copied instead of box container") {
                auto v = s->get(pow);
                REQUIRE_FALSE(v.isContainer()); // boxed value would appear as a container here
                REQUIRE(v.isScalar());
                REQUIRE(v.getInt() == 5);
            }
        }
        // Probably belongs elsewhere
        WHEN("Boxing a value") {
            auto el = data::StructElement(5);
            auto b = el.getBoxed();
            auto el2 = data::StructElement(b);
            THEN("Box contents behaves in meaningful way") {
                REQUIRE(el2.isContainer());
                REQUIRE_FALSE(el2.isScalar());
                REQUIRE(el2.unbox().isScalar());
                REQUIRE(el2.getInt() == 5);
            }
        }
    }
}

// NOLINTEND
