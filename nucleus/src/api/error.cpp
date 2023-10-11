#include <c_api.h>

namespace error {
    //
    // lastError can be assumed to be a string ord, but for these functions,
    // it's simple a thread local integer
    //
    static thread_local uint32_t lastError = 0;
} // namespace error

uint32_t ggapiGetError() {
    return error::lastError;
}

void ggapiSetError(uint32_t code) {
    error::lastError = code;
}
