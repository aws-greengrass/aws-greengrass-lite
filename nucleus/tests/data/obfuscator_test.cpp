#include "data/data_util.hpp"
#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN
SCENARIO("Obfuscator is reversable", "[obfuscator]") {
    GIVEN("A special ID") {
        uint32_t id = data::IdObfuscator::INVALID_ID;
        WHEN("Obfuscating the ID") {
            uint32_t obf = data::IdObfuscator::obfuscate(id);
            THEN("ID maps to special constant") {
                REQUIRE(obf == data::IdObfuscator::INVALID_OBFUSCATED_ID);
            }
        }
    }
    GIVEN("A special obfuscated ID") {
        uint32_t obf = data::IdObfuscator::INVALID_OBFUSCATED_ID;
        WHEN("Deobfuscating the ID") {
            uint32_t id = data::IdObfuscator::deobfuscate(obf);
            THEN("ID maps to special constant") {
                REQUIRE(id == data::IdObfuscator::INVALID_ID);
            }
        }
    }
    GIVEN("A set of IDs") {
        auto id1 =
            GENERATE(1, 2, 3, 4, 5, 0x11, 0x121, 0x1234, 0x12345, 0x654321, 0x7654321, 0x87651234);
        WHEN("Round-tripping the ID") {
            uint32_t obf = data::IdObfuscator::obfuscate(id1);
            uint32_t id2 = data::IdObfuscator::deobfuscate(obf);
            THEN("ID gives the intended value") {
                REQUIRE(id1 == id2);
            }
            THEN("ID differs from obfuscated value") {
                // Technically there may be an id1 picked that id1==obf
                // That's ok, change test set
                // But if it's happening for many IDs, that's an issue
                REQUIRE(id1 != obf);
            }
        }
    }
}

// NOLINTEND
