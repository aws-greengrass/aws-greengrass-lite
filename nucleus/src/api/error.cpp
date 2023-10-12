#include <cpp_api.hpp>

namespace error {
    //
    // lastError can be assumed to be a string ord, but for these functions,
    // it's simple a thread local integer
    //
    uint32_t getSetLastError(uint32_t newValue, bool write) {
        static thread_local uint32_t lastError = 0;
        uint32_t current = lastError;
        if(write) {
            lastError = newValue;
        }
        return current;
    }
} // namespace error

uint32_t ggapiGetError() noexcept {
    return error::getSetLastError(0, false);
}

void ggapiSetError(uint32_t code) {
    error::getSetLastError(code, true);
}
