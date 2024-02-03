#pragma once
#include "api_errors.hpp"
#include "api_forwards.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace ggapi {
    class CallbackManager;

    // Needs extern "C" as that makes static functions use C linkage, and callback is passed to
    // ggapiRegisterCallback which uses C linkage. CallbackManager can't be marked extern C as it
    // has templates and other static function members.
    extern "C" class CallbackManagerCCallback {
        friend CallbackManager;

        /**
         * Round-trip point of entry that was passed to Nucleus for Nucleus to use when performing
         * a callback.
         *
         * @param callbackContext Round-trip context, large enough to hold a pointer
         * @param callbackType Callback type, indicating what structure was passed
         * @param callbackDataSize Size of structure for validation
         * @param callbackData Pointer to structure based on previous fields
         */
        static ggapiErrorKind callback(
            ggapiContext callbackContext,
            ggapiSymbol callbackType,
            ggapiDataLen callbackDataSize,
            void *callbackData) noexcept;
    };

    /**
     * Factory to serve out callback handles allowing rich C++ style callbacks while maintaining
     * a C interface to the API. Note that while this supports lambdas, lambdas should be used
     * with caution. Better to use a callback function and callback parameters (see std::thread).
     */
    class CallbackManager {
        friend CallbackManagerCCallback;

    public:
        using Delegate = std::function<void()>;

        /**
         * Base class for dispatch classes. Subclasses need to implement prepare to construct
         * a delegate lambda that will invoke the callback implementation.
         */
        struct CallbackDispatch {
            /**
             * Implement to creates a new lambda that wraps saved callback, ready to be called. This
             * operation occurs inside a lock so the new lambda is used after releasing the lock.
             *
             * @param callbackType The 'type' of callback for validation
             * @param size Size of callback structure for validation
             * @param data Anonymous pointer to callback structure
             * @return Delegate lambda ready for calling
             */
            [[nodiscard]] virtual Delegate prepare(
                Symbol callbackType, ggapiDataLen size, void *data) const = 0;
            /**
             * Expected callback type for validation
             */
            [[nodiscard]] virtual Symbol type() const = 0;

            CallbackDispatch() = default;
            CallbackDispatch(const CallbackDispatch &) = default;
            CallbackDispatch(CallbackDispatch &&) = default;
            CallbackDispatch &operator=(const CallbackDispatch &) = default;
            CallbackDispatch &operator=(CallbackDispatch &&) = default;
            virtual ~CallbackDispatch() = default;

            void assertCallbackType(Symbol actual) const {
                if(actual != type()) {
                    throw std::runtime_error(
                        "Mismatch callback type - received " + actual.toString() + " instead of "
                        + type().toString());
                }
            }

            /**
             * The structure passed to the plugin from the Nucleus is anonymous. We know how
             * to interpret this structure based on (1) matching context, (2) matching type,
             * and (3) checking that the passed in structure is not too small. The passed in
             * structure can be bigger if, for example, a newer version of Nucleus adds additional
             * context, in which case, that additional context is ignored by older plugins.
             * @tparam T Expected structure type
             * @param size Size of structure reported by Nucleus
             * @param data Anonymous pointer to structure
             * @return Mutable structure after trivial validation
             */
            template<typename T>
            static T &checkedStruct(uint32_t size, void *data) {
                if(data == nullptr) {
                    throw std::runtime_error("Null pointer provided to callback");
                }
                if(size < sizeof(T)) {
                    throw std::runtime_error(
                        "Structure size error - maybe running with earlier version of Nucleus");
                }
                // Note, larger structure is ok - expectation is that new fields are added to end
                // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
                return *reinterpret_cast<T *>(data);
            }
        };

    private:
        std::shared_mutex _mutex;
        std::map<uintptr_t, std::unique_ptr<const CallbackDispatch>> _callbacks;

        ggapiErrorKind callback(
            ggapiContext callbackContext,
            ggapiSymbol callbackType,
            ggapiDataLen callbackDataSize,
            void *callbackData) noexcept {

            if(callbackType == 0) {
                // Nucleus indicates callback is no longer required
                std::unique_lock guard{_mutex};
                _callbacks.erase(callbackContext);
                return 0;
            } else {
                // Prepare the call
                std::shared_lock guard{_mutex};
                // We could enable a fast "unsafe" option that just casts the callbackContext
                // to a pointer. For now, this acts as a robust double-check.
                const auto &cb = _callbacks.at(callbackContext);
                // Pre-process callback while lock is held
                auto delegate = cb->prepare(Symbol(callbackType), callbackDataSize, callbackData);
                guard.unlock();
                // Actual call
                return catchErrorToKind(delegate);
            }
        }

        ObjHandle wrapHelper(std::unique_ptr<CallbackDispatch> cb) {
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            auto idx = reinterpret_cast<uintptr_t>(cb.get());
            auto type = cb->type();
            std::unique_lock guard{_mutex};
            _callbacks.emplace(idx, std::move(cb));
            ggapiObjHandle callbackHandle = 0;
            callApiThrowError(
                ::ggapiRegisterCallback,
                &CallbackManagerCCallback::callback,
                idx,
                type.asInt(),
                &callbackHandle);
            return ObjHandle(callbackHandle);
        }

    public:
        /**
         * Register callback with Nucleus. The handle will be used to re-reference the callback
         * for the intended function. The handle only needs local scope, as the Nucleus maintains
         * the correct scope to hold on to the callback. Note, there is no way to prevent the
         * actual callback function becoming invalid after this call. That all depends on C++
         * scoping rules.
         *
         * @param cb Callback object that is used to dispatch to actual callback
         * @return Typed Handle to registered callback
         */
        template<typename CallbackType>
        CallbackType registerWithNucleus(std::unique_ptr<CallbackDispatch> cb) {
            return CallbackType(wrapHelper(std::move(cb)));
        }

        /**
         * Singleton
         */
        static CallbackManager &self() {
            static CallbackManager singleton{};
            return singleton;
        }
    };

    extern "C" inline ggapiErrorKind CallbackManagerCCallback::callback(
        ggapiContext callbackContext,
        ggapiSymbol callbackType,
        ggapiDataLen callbackDataSize,
        void *callbackData) noexcept {

        return CallbackManager::self().callback(
            callbackContext, callbackType, callbackDataSize, callbackData);
    }
} // namespace ggapi
