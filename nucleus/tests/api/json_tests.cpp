#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>
#include <temp_module.hpp>

// NOLINTBEGIN
SCENARIO("Json conversion tests", "[json][list][struct][buffer]") {
    const auto json = R"( {"Alpha":5, "Beta":6, "Gamma":{"A":"a","B":"b"}, "Delta":[1,2,3,4]} )";
    const auto jsonList = R"( [1,2,3,4] )";
    const auto jsonLiteral = R"( "Foo" )";
    const auto invalidJson = R"( })";
    const auto emptyJson = R"( )";
    scope::LocalizedContext forTesting{};
    util::TempModule testModule("json-test");

    GIVEN("A buffer containing a JSON structure") {
        ggapi::Buffer buffer = ggapi::Buffer::create();
        buffer.put(0, std::string_view(json));
        WHEN("Parsing as JSON") {
            ggapi::Container c = buffer.fromJson();
            THEN("A structure can be retrieved") {
                ggapi::Struct s(c);
                REQUIRE(s.get<int>("Alpha") == 5);
                REQUIRE(s.get<int>("Beta") == 6);
                ggapi::Container gamma = s.get<ggapi::Container>("Gamma");
                REQUIRE(gamma.isStruct());
                ggapi::Struct g(gamma);
                REQUIRE(g.get<std::string>("A") == "a");
                REQUIRE(g.get<std::string>("B") == "b");
                REQUIRE(g.size() == 2);
                ggapi::Container delta = s.get<ggapi::Container>("Delta");
                REQUIRE(gamma.isStruct());
                ggapi::List d(delta);
                REQUIRE(d.get<int>(0) == 1);
                REQUIRE(d.get<int>(1) == 2);
                REQUIRE(d.get<int>(2) == 3);
                REQUIRE(d.get<int>(3) == 4);
                REQUIRE(d.size() == 4);
                REQUIRE(s.size() == 4);
            }
            AND_WHEN("Structure is converted to JSON again") {
                ggapi::Buffer buffer2 = c.toJson();
                THEN("Second structure is equivalent to first") {
                    ggapi::Container c2 = buffer2.fromJson();
                    ggapi::Struct s2(c2);
                    REQUIRE(s2.get<int>("Alpha") == 5);
                    REQUIRE(s2.get<int>("Beta") == 6);
                    ggapi::Container gamma = s2.get<ggapi::Container>("Gamma");
                    REQUIRE(gamma.isStruct());
                    ggapi::Struct g(gamma);
                    REQUIRE(g.get<std::string>("A") == "a");
                    REQUIRE(g.get<std::string>("B") == "b");
                    REQUIRE(g.size() == 2);
                    ggapi::Container delta = s2.get<ggapi::Container>("Delta");
                    REQUIRE(gamma.isStruct());
                    ggapi::List d(delta);
                    REQUIRE(d.get<int>(0) == 1);
                    REQUIRE(d.get<int>(1) == 2);
                    REQUIRE(d.get<int>(2) == 3);
                    REQUIRE(d.get<int>(3) == 4);
                    REQUIRE(d.size() == 4);
                    REQUIRE(s2.size() == 4);
                }
            }
        }
    }
    GIVEN("A buffer containing a JSON list") {
        ggapi::Buffer buffer = ggapi::Buffer::create();
        buffer.put(0, std::string_view(jsonList));
        WHEN("Parsing as JSON") {
            ggapi::Container c = buffer.fromJson();
            THEN("A list can be retrieved") {
                ggapi::List list(c);
                REQUIRE(list.get<int>(0) == 1);
                REQUIRE(list.get<int>(1) == 2);
                REQUIRE(list.get<int>(2) == 3);
                REQUIRE(list.get<int>(3) == 4);
                REQUIRE(list.size() == 4);
            }
            AND_WHEN("Structure is converted to JSON again") {
                ggapi::Buffer buffer2 = c.toJson();
                THEN("Second list is equivalent to first") {
                    ggapi::Container c2 = buffer2.fromJson();
                    ggapi::List list2(c2);
                    REQUIRE(list2.get<int>(0) == 1);
                    REQUIRE(list2.get<int>(1) == 2);
                    REQUIRE(list2.get<int>(2) == 3);
                    REQUIRE(list2.get<int>(3) == 4);
                    REQUIRE(list2.size() == 4);
                }
            }
        }
    }
    GIVEN("A buffer containing a JSON literal") {
        ggapi::Buffer buffer = ggapi::Buffer::create();
        buffer.put(0, std::string_view(jsonLiteral));
        WHEN("Parsing as JSON") {
            ggapi::Container c = buffer.fromJson();
            THEN("Literal is boxed, retrieve via unbox function") {
                REQUIRE(c.isScalar());
                REQUIRE(c.unbox<std::string>() == "Foo");
                REQUIRE(c.size() == 1);
            }
        }
    }
    GIVEN("A buffer containing invalid JSON") {
        ggapi::Buffer buffer = ggapi::Buffer::create();
        buffer.put(0, std::string_view(invalidJson));
        WHEN("Parsing as JSON") {
            THEN("An exception is thrown") {
                REQUIRE_THROWS_AS(buffer.fromJson(), ggapi::GgApiError);
            }
        }
    }
    GIVEN("A buffer containing empty JSON") {
        ggapi::Buffer buffer = ggapi::Buffer::create();
        buffer.put(0, std::string_view(emptyJson));
        WHEN("Parsing as JSON") {
            ggapi::Container c = buffer.fromJson();
            THEN("An empty container is returned") {
                REQUIRE_FALSE(c);
            }
        }
    }
}
// NOLINTEND
