#pragma once
#include "errors/errors.hpp"
#include <cpp_api.hpp>
#include <functional>

namespace apiImpl {
    template<typename Func, typename... Args>
    inline uint32_t catchErrorToKind(Func &&f, Args &&...args) {
        try {
            std::invoke(std::forward<Func>(f), std::forward<Args>(args)...);
            return 0;
        } catch(errors::Error &err) {
            return err.toThreadLastError();
        } catch(ggapi::GgApiError &err) {
            return errors::Error::of<ggapi::GgApiError>(err).toThreadLastError();
        } catch(std::exception &err) {
            return errors::Error::of<std::exception>(err).toThreadLastError();
        } catch(...) {
            return errors::Error::unspecified().toThreadLastError();
        }
    }

    inline void setBool(ggapiBool *pBool, bool test) {
        *pBool = test ? 1 : 0;
    }

} // namespace apiImpl
