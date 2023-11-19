#include "errors/error_base.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN
SCENARIO("Last error is invariant in thread", "[errors]") {
    GIVEN("Thread is in an error state") {
        std::runtime_error cpp_err{"Some error text"};
        errors::Error err{errors::Error::of(cpp_err)};
        errors::ThreadErrorContainer::get().setError(err);
        WHEN("Error is retrieved") {
            auto gotErr = errors::ThreadErrorContainer::get().getError();
            THEN("Retrieved error is valid") {
                REQUIRE(gotErr.has_value());
                // The kind name is internal, and depends on implementation detail
                // The platform safe way is to use typeid
                // When manually testing, this was string like "std13runtime_error"
                std::string kindName = gotErr.value().kind().toString();
                REQUIRE(kindName == typeid(std::runtime_error).name());
                REQUIRE(
                    std::string_view(gotErr.value().what()) == std::string_view("Some error text"));
            }
        }
        WHEN("Error 'what' is retrieved via API") {
            auto what1 = ::ggapiGetErrorWhat();
            THEN("'What' string is valid and null terminated") {
                REQUIRE(what1 != nullptr);
                REQUIRE(std::string(what1) == "Some error text");
            }
        }
        WHEN("Error 'what' is retrieved back to back") {
            auto what1 = ::ggapiGetErrorWhat();
            auto what2 = ::ggapiGetErrorWhat();
            THEN("What string pointer is unchanged and consistent") {
                // Yes, these are pointers
                // Yes, these are c_str() style strings - see std::exception
                REQUIRE(static_cast<const void *>(what1) == static_cast<const void *>(what2));
            }
        }
    }
}
// NOLINTEND
