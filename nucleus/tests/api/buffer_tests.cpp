#include "scope/context_full.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <cpp_api.hpp>
#include <sstream>
#include <temp_module.hpp>

// NOLINTBEGIN
SCENARIO("Buffer API", "[buffer]") {
    scope::LocalizedContext forTesting{};
    util::TempModule testModule("buffer-test");

    GIVEN("A buffer") {
        auto buf = ggapi::Buffer::create();

        THEN("Buffer is empty") {
            REQUIRE(buf.size() == 0);
        }
        WHEN("Data is stuffed into a buffer via streams") {
            buf.out() << "Hello\nWorld\n" << std::flush;
            THEN("Buffer is of expected length") {
                REQUIRE(buf.size() == 12);
            }
            THEN("Buffer contains stuffed data") {
                auto str{buf.get<std::string>(0, 100)};
                REQUIRE(str == "Hello\nWorld\n");
            }
            THEN("Buffer can be extracted in part via streams") {
                auto strm{buf.in()};
                strm.seekg(6, std::ios_base::beg);
                std::ostringstream oss;
                strm >> oss.rdbuf();
                REQUIRE(oss.str() == "World\n");
            }
            THEN("Buffer can be extracted in part to vector") {
                std::vector<std::byte> vec(3);
                buf.get(6, vec);
                std::vector<char> vec2;
                for(const auto &i : vec) {
                    vec2.push_back((char) i);
                }
                REQUIRE_THAT(vec2, Catch::Matchers::Equals(std::vector<char>{'W', 'o', 'r'}));
            }
            THEN("Buffer can be extracted in part to string") {
                std::string str;
                str.resize(10);
                buf.get(6, str);
                REQUIRE(str == "World\n");
            }
        }
        AND_GIVEN("Buffer has some data") {
            buf.put(0, std::string_view("Hello\nWorld\n1234567890\n"));

            WHEN("Data is replaced with absolute seeks") {
                auto strm = buf.out();
                strm.seekp(6);
                strm << "Wish\nWash\n";
                strm.seekp(11);
                strm << "Sound\n" << std::flush;

                THEN("Buffer data is as expected") {
                    REQUIRE(buf.get<std::string>(0, 200) == "Hello\nWish\nSound\n67890\n");
                }
            }
            WHEN("Data is placed with relative seeks") {
                auto strm = buf.out();
                strm.seekp(-11, std::ios_base::end);
                strm << "Bar";
                strm.seekp(-9, std::ios_base::cur);
                strm << "Bing";
                strm.flush();

                THEN("Buffer data is as expected") {
                    // spell-checker: disable-next-line
                    REQUIRE(buf.get<std::string>(0, 200) == "Hello\nBingd\nBar4567890\n");
                }
            }
        }
        AND_GIVEN("Buffer has some numbers") {
            buf.put(0, std::string_view("10\n20\n30\n40"));
            WHEN("Reading from stream") {
                auto strm = buf.in();
                int i1, i2, i3, i4;
                strm >> i1 >> i2 >> i3 >> i4;
                THEN("Numbers are valid") {
                    REQUIRE(i1 == 10);
                    REQUIRE(i2 == 20);
                    REQUIRE(i3 == 30);
                    REQUIRE(i4 == 40);
                }
            }
        }
    }
}

// NOLINTEND
