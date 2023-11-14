#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>

static ggapi::Struct simpleListener(ggapi::Task, ggapi::StringOrd, ggapi::Struct) {
    return ggapi::Struct{0};
}

// NOLINTBEGIN
SCENARIO("Shared structure API", "[struct]") {
    scope::LocalizedScope forTesting{scope::Context::create()};

    GIVEN("A structure") {
        auto s = ggapi::Struct::create();
        ggapi::StringOrd ping{"ping"};
        ggapi::StringOrd pow{"pow"};

        THEN("Structure is empty") {
            REQUIRE(s.size() == 0);
        }
        WHEN("Items are added to structure key by key") {
            // temporary to try and understand why automated build fails KV init list
            s.put("foo", 1);
            s.put(ping, 3);
            s.put("zing", 4.6);
            s.put("zap", "zooey");
            s.put(pow, pow);
            THEN("Structure size increased") {
                REQUIRE(s.size() == 5);
            }
            THEN("Structure contents are as expected") {
                REQUIRE(s.get<int>("foo") == 1);
                REQUIRE(s.get<int>("ping") == 3);
                REQUIRE(s.get<double>("zing") == 4.6);
                REQUIRE(s.get<std::string>("zap") == "zooey");
                REQUIRE(s.get<std::string>("pow") == "pow");
            }
        }
        WHEN("Items are added with simple initialize list") {
            // temporary to try and understand why automated build fails KV init list
            s.put({"foo", 1});
            s.put({{ping, 3}});
            THEN("Structure size increased") {
                REQUIRE(s.size() == 2);
            }
            THEN("Structure contents are as expected") {
                REQUIRE(s.get<int>("foo") == 1);
                REQUIRE(s.get<int>("ping") == 3);
            }
        }
        WHEN("Items are added to structure") {
            s.put("foo", 1).put("baz", 10);
            s.put({
                {"bar",  2      },
                {ping,   3      },
                {"zing", 4.6    },
                {"zap",  "zooey"},
                {pow,    pow    }
            });
            THEN("Structure size increased") {
                REQUIRE(s.size() == 7);
            }
            THEN("Structure contents are as expected") {
                REQUIRE(s.get<int>("foo") == 1);
                REQUIRE(s.get<int>("baz") == 10);
                REQUIRE(s.get<int>("bar") == 2);
                REQUIRE(s.get<int>("ping") == 3);
                REQUIRE(s.get<double>("zing") == 4.6);
                REQUIRE(s.get<std::string>("zap") == "zooey");
                REQUIRE(s.get<std::string>("pow") == "pow");
            }
            AND_WHEN("A key of same name is used") {
                s.put("ping", true);
                THEN("The item is replaced in structure") {
                    REQUIRE(s.size() == 7);
                    REQUIRE(s.get<bool>(ping) == true);
                }
            }
            AND_WHEN("A key of mixed-case is used") {
                s.put("Ping", 30);
                THEN("The item is unique in structure") {
                    REQUIRE(s.size() == 8);
                    REQUIRE(s.get<int>(ping) == 3);
                    REQUIRE(s.get<int>("ping") == 3);
                    REQUIRE(s.get<int>("Ping") == 30);
                }
            }
            AND_WHEN("An interned string is used internally") {
                s.put("ping", 10);
                s.put("ping", pow.toString());
                THEN("The item is replaced in structure as expected") {
                    REQUIRE(s.size() == 7);
                    REQUIRE(s.get<std::string>(ping) == "pow");
                }
            }
        }
        WHEN("A listener is added to structure") {
            ggapi::CallScope scope = ggapi::CallScope::newCallScope();
            ggapi::Subscription handle{scope.subscribeToTopic({}, simpleListener)};
            s.put("Listener", handle);
            THEN("Listener can be retrieved from structure") {
                ggapi::Subscription other = s.get<ggapi::Subscription>("Listener");
                REQUIRE(static_cast<bool>(other)); // a handle is given
                REQUIRE(handle != other); // handles are independent
                REQUIRE(handle.isSameObject(other)); // handles are for same object
            }
        }
    }
}

// NOLINTEND
