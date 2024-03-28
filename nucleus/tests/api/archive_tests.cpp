#include "scope/context_full.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <cpp_api.hpp>
#include <sstream>
#include <temp_module.hpp>

// NOLINTBEGIN
SCENARIO("Data archive/dearchive", "[archive]") {
    scope::LocalizedContext forTesting{};
    util::TempModule testModule("archive-test");

    GIVEN("A basic data structure") {
        struct SomeData : public ggapi::Serializable {
            int x{0};
            double y{0};
            std::optional<double> y2;
            std::string str;
            std::optional<std::string> str2;

            void visit(ggapi::Archive &archive) override {
                archive("x", x);
                archive("y", y);
                archive("y2", y2);
                archive("str", str);
                archive("str2", str2);
            }
        };

        WHEN("Dearchiving from struct") {
            auto src = ggapi::Struct::create().put(
                {{"x", 5}, {"y", 1.0}, {"y2", 2.0}, {"str", "foo"}, {"str2", "bar"}});
            SomeData data;
            ggapi::Archive::transform<ggapi::ContainerDearchiver>(data, src);
            THEN("Data was dearchived correctly") {
                REQUIRE(data.x == 5);
                REQUIRE(data.y == 1.0);
                REQUIRE(data.y2.has_value());
                REQUIRE(data.y2.value() == 2.0);
                REQUIRE(data.str == "foo");
                REQUIRE(data.str2.has_value());
                REQUIRE(data.str2.value() == "bar");
            }
        }

        WHEN("Dearchiving empty struct") {
            auto src = ggapi::Struct::create();
            SomeData data;
            ggapi::Archive::transform<ggapi::ContainerDearchiver>(data, src);
            THEN("Data was set to reasonable defaults") {
                REQUIRE(data.x == 0);
                REQUIRE(data.y == 0.0);
                REQUIRE_FALSE(data.y2.has_value());
                REQUIRE(data.str == "");
                REQUIRE_FALSE(data.str2.has_value());
            }
        }

        WHEN("Archiving filled out struct") {
            auto dest = ggapi::Struct::create();
            SomeData data;
            data.x = 5;
            data.y = 1.0;
            data.y2 = 2.0;
            data.str = "foo";
            data.str2 = "bar";
            ggapi::Archive::transform<ggapi::StructArchiver>(data, dest);
            THEN("Data was archived correctly") {
                REQUIRE(dest.get<int>("x") == 5);
                REQUIRE(dest.get<double>("y") == 1.0);
                REQUIRE(dest.get<double>("y2") == 2.0);
                REQUIRE(dest.get<std::string>("str") == "foo");
                REQUIRE(dest.get<std::string>("str2") == "bar");
                REQUIRE(dest.size() == 5);
            }
        }

        WHEN("Archiving partial struct") {
            auto dest = ggapi::Struct::create();
            SomeData data;
            data.x = 5;
            data.y = 1.0;
            data.str = "foo";
            ggapi::Archive::transform<ggapi::StructArchiver>(data, dest);
            THEN("Data was archived correctly") {
                REQUIRE(dest.get<int>("x") == 5);
                REQUIRE(dest.get<double>("y") == 1.0);
                REQUIRE(dest.get<std::string>("str") == "foo");
                REQUIRE(dest.size() == 3);
            }
        }
    }

    GIVEN("Nested structures") {
        struct InnerData : public ggapi::Serializable {
            int a{0};
            std::optional<double> b;

            void visit(ggapi::Archive &archive) override {
                archive("a", a);
                archive("b", b);
            }
        };

        struct OuterData : public ggapi::Serializable {
            int x{0};
            std::optional<double> y;
            std::shared_ptr<InnerData> inner1;
            std::optional<InnerData> inner2;
            InnerData inner3;

            void visit(ggapi::Archive &archive) override {
                archive("x", x);
                archive("y", y);
                archive("inner1", inner1);
                archive("inner2", inner2);
                archive("inner3", inner3);
            }
        };

        WHEN("Dearchiving from struct") {
            auto inner1Src = ggapi::Struct::create().put(
                {{"a", 1}, {"b", 1.0}});
            auto inner2Src = ggapi::Struct::create().put(
                {{"a", 2}});
            auto inner3Src = ggapi::Struct::create().put(
                {{"a", 3}});
            auto outerSrc = ggapi::Struct::create().put(
                {{"x", 5}, {"y", 10.0}, {"inner1", inner1Src}, {"inner2", inner2Src}, {"inner3", inner3Src}});
            OuterData data;
            ggapi::Archive::transform<ggapi::ContainerDearchiver>(data, outerSrc);
            THEN("Data was dearchived correctly") {
                REQUIRE(data.x == 5);
                REQUIRE(data.y.has_value());
                REQUIRE(data.y.value() == 10.0);
                REQUIRE(data.inner1.operator bool());
                REQUIRE(data.inner1->a == 1);
                REQUIRE(data.inner1->b.has_value());
                REQUIRE(data.inner1->b.value() == 1.0);
                REQUIRE(data.inner2.has_value());
                REQUIRE(data.inner2->a == 2);
                REQUIRE(data.inner3.a == 3);
            }
        }

        WHEN("Dearchiving empty struct") {
            auto src = ggapi::Struct::create();
            OuterData data;
            ggapi::Archive::transform<ggapi::ContainerDearchiver>(data, src);
            THEN("Shared pointer is empty") {
                REQUIRE_FALSE(data.inner1);
            }
            THEN("Optional is empty") {
                REQUIRE_FALSE(data.inner2);
            }
            THEN("Nested has default") {
                REQUIRE(data.inner3.a == 0);
            }
        }

        WHEN("Archiving nested structures") {
            auto dest = ggapi::Struct::create();
            OuterData data;
            data.x = 5;
            data.y = 10.0;
            ggapi::Archive::transform<ggapi::StructArchiver>(data, dest);
            THEN("Data was archived correctly") {
                REQUIRE(dest.get<int>("x") == 5);
                REQUIRE(dest.get<double>("y") == 10.0);
                REQUIRE(dest.hasKey("inner3"));
                REQUIRE(dest.size() == 3);
                auto i3 = dest.get<ggapi::Struct>("inner3");
                REQUIRE(i3.get<int>("a") == 0);
                REQUIRE(i3.size() == 1);
            }
            AND_WHEN("Expanding to include inner structures") {
                data.inner1 = std::make_shared<InnerData>();
                data.inner1->a = 1;
                data.inner2.emplace(InnerData{});
                data.inner2->a = 2;
                data.inner3.a = 3;
                ggapi::Archive::transform<ggapi::StructArchiver>(data, dest);
                THEN("Inner1 was archived correctly") {
                    auto inner1Dest = dest.get<ggapi::Struct>("inner1");
                    REQUIRE(inner1Dest);
                    REQUIRE(inner1Dest.get<int>("a") == 1);
                }
                THEN("Inner2 was archived correctly") {
                    auto inner2Dest = dest.get<ggapi::Struct>("inner2");
                    REQUIRE(inner2Dest);
                    REQUIRE(inner2Dest.get<int>("a") == 2);
                }
                THEN("Inner3 was archived correctly") {
                    auto inner3Dest = dest.get<ggapi::Struct>("inner3");
                    REQUIRE(inner3Dest);
                    REQUIRE(inner3Dest.get<int>("a") == 3);
                }
            }
        }
    }

    GIVEN("A struct with a list") {
        struct InnerData : public ggapi::Serializable {
            int a{0};

            InnerData() {}
            InnerData(int i) : a(i) {}

            void visit(ggapi::Archive & archive) override {
                archive("a", a);
            }
        };

        struct OuterData : public ggapi::Serializable {
            std::vector<int> list1;
            std::vector<InnerData> list2;
            std::list<InnerData> list3;

            void visit(ggapi::Archive &archive) override {
                archive("list1", list1);
                archive("list2", list2);
                archive("list3", list3);
            }
        };

        WHEN("Dearchiving from struct") {
            auto inner1Src = ggapi::Struct::create().put({"a", 10});
            auto inner2Src = ggapi::Struct::create().put({"a", 20});
            auto inner3Src = ggapi::Struct::create().put({"a", 30});
            auto list1Src = ggapi::List::create().append(
                {1,2,3});
            auto list2Src = ggapi::List::create().append(
                {inner1Src, inner2Src, inner3Src});
            auto list3Src = ggapi::List::create().append(
                {inner2Src, inner3Src});
            auto outerSrc = ggapi::Struct::create().put(
                {{"x", 5}, {"list1", list1Src}, {"list2", list2Src}, {"list3", list3Src}});
            OuterData data;
            ggapi::Archive::transform<ggapi::ContainerDearchiver>(data, outerSrc);
            THEN("Data was dearchived correctly") {
                REQUIRE(data.list1.size() == 3);
                REQUIRE(data.list1.at(0) == 1);
                REQUIRE(data.list1.at(1) == 2);
                REQUIRE(data.list1.at(2) == 3);
                REQUIRE(data.list2.size() == 3);
                REQUIRE(data.list2.at(0).a == 10);
                REQUIRE(data.list2.at(1).a == 20);
                REQUIRE(data.list2.at(2).a == 30);
                REQUIRE(data.list3.size() == 2);
                REQUIRE(data.list3.front().a == 20);
                REQUIRE(data.list3.back().a == 30);
            }
        }

        WHEN("Dearchiving empty struct") {
            auto src = ggapi::Struct::create();
            OuterData data;
            ggapi::Archive::transform<ggapi::ContainerDearchiver>(data, src);
            THEN("Lists are empty") {
                REQUIRE(data.list1.size() == 0);
                REQUIRE(data.list2.size() == 0);
                REQUIRE(data.list3.size() == 0);
            }
        }

        WHEN("Archiving lists") {
            auto dest = ggapi::Struct::create();
            OuterData data;
            data.list1.push_back(1);
            data.list1.push_back(2);
            data.list1.push_back(3);
            data.list2.push_back(InnerData{10});
            data.list2.push_back(InnerData{20});
            data.list3.push_back(InnerData{100});
            ggapi::Archive::transform<ggapi::StructArchiver>(data, dest);
            THEN("List1 was archived correctly") {
                auto lst = dest.get<ggapi::List>("list1");
                REQUIRE(lst.size() == 3);
                REQUIRE(lst.get<int>(0) == 1);
                REQUIRE(lst.get<int>(1) == 2);
                REQUIRE(lst.get<int>(2) == 3);
            }
            THEN("List2 was archived correctly") {
                auto lst = dest.get<ggapi::List>("list2");
                REQUIRE(lst.size() == 2);
                auto s1 = lst.get<ggapi::Struct>(0);
                REQUIRE(s1.get<int>("a") == 10);
                auto s2 = lst.get<ggapi::Struct>(1);
                REQUIRE(s2.get<int>("a") == 20);
            }
            THEN("List3 was archived correctly") {
                auto lst = dest.get<ggapi::List>("list3");
                REQUIRE(lst.size() == 1);
                auto s1 = lst.get<ggapi::Struct>(0);
                REQUIRE(s1.get<int>("a") == 100);
            }
        }
    }

    GIVEN("Structure with a map") {
        struct InnerData : public ggapi::Serializable {
            int a{0};

            InnerData() {}
            InnerData(int i) : a(i) {}

            void visit(ggapi::Archive &archive) override {
                archive("a", a);
            }
        };

        struct OuterData : public ggapi::Serializable {
            std::map<std::string, InnerData> map1;
            std::unordered_map<std::string, std::string> map2;
            ggapi::Struct map3;

            void visit(ggapi::Archive &archive) override {
                archive("map1", map1);
                archive("map2", map2);
                archive("map3", map3);
            }
        };

        WHEN("Dearchiving from struct") {
            auto inner1Src = ggapi::Struct::create().put(
                {{"a", 1}});
            auto inner2Src = ggapi::Struct::create().put(
                {{"a", 2}});
            auto inner3Src = ggapi::Struct::create().put(
                {{"a", 3}});
            auto map1Src = ggapi::Struct::create().put(
                {{"a", inner1Src}, {"b", inner2Src}, {"c", inner3Src}}
                );
            auto map2Src = ggapi::Struct::create().put(
                {{"a", 10}, {"b", 20}, {"c", 30}}
            );
            auto map3Src = ggapi::Struct::create().put(
                {{"a", inner1Src}, {"b", 20}}
            );
            auto outerSrc = ggapi::Struct::create().put(
                {{"map1", map1Src}, {"map2", map2Src}, {"map3", map3Src}});
            OuterData data;
            ggapi::Archive::transform<ggapi::ContainerDearchiver>(data, outerSrc);
            THEN("Map1 was dearchived correctly") {
                REQUIRE(data.map1.size() == 3);
                REQUIRE(data.map1.at("a").a == 1);
                REQUIRE(data.map1.at("b").a == 2);
                REQUIRE(data.map1.at("c").a == 3);
            }
            THEN("Map2 was dearchived correctly") {
                // Note implicit conversion from integer to string to conform to data type
                REQUIRE(data.map2.size() == 3);
                REQUIRE(data.map2.at("a") == "10");
                REQUIRE(data.map2.at("b") == "20");
                REQUIRE(data.map2.at("c") == "30");
            }
            THEN("Map3 was dearchived correctly") {
                REQUIRE(data.map3);
                REQUIRE(data.map3.size() == 2);
                auto aa = data.map3.get<ggapi::Struct>("a");
                REQUIRE(aa.get<int>("a") == 1);
                REQUIRE(data.map3.get<int>("b") == 20);
            }
        }

        WHEN("Dearchiving empty struct") {
            auto src = ggapi::Struct::create();
            OuterData data;
            ggapi::Archive::transform<ggapi::ContainerDearchiver>(data, src);
            THEN("Maps are empty") {
                REQUIRE(data.map1.size() == 0);
                REQUIRE(data.map2.size() == 0);
            }
            THEN("Nested Struct initialized to empty") {
                REQUIRE(data.map3);
                REQUIRE(data.map3.size() == 0);
            }
        }

        WHEN("Archiving maps") {
            auto dest = ggapi::Struct::create();
            OuterData data;
            data.map1.emplace("a", InnerData{1});
            data.map2.emplace("b", "foo");
            data.map3 = ggapi::Struct::create().put(
                {{"c", 30}}
            );
            ggapi::Archive::transform<ggapi::StructArchiver>(data, dest);
            THEN("Map1 was archived correctly") {
                auto map1 = dest.get<ggapi::Struct>("map1");
                REQUIRE(map1);
                REQUIRE(map1.size() == 1);
                REQUIRE(map1.get<ggapi::Struct>("a").get<int>("a") == 1);
            }
            THEN("Map2 was archived correctly") {
                auto map2 = dest.get<ggapi::Struct>("map2");
                REQUIRE(map2);
                REQUIRE(map2.size() == 1);
                REQUIRE(map2.get<std::string>("b") == "foo");
            }
            THEN("Map3 was archived correctly") {
                auto map3 = dest.get<ggapi::Struct>("map3");
                REQUIRE(map3);
                REQUIRE(map3.size() == 1);
                REQUIRE(map3.get<int>("c") == 30);
            }
        }
    }

}
// NOLINTEND
