#pragma once
#include <functional>
#include <memory>

namespace util {

    namespace auto_release_impl {
        template<typename ResourceT>
        struct BasicTraits {
            [[nodiscard]] static bool isValid(const ResourceT &item) {
                return static_cast<bool>(item);
            }
            [[nodiscard]] static ResourceT empty() {
                return ResourceT{};
            }
        };
        template<typename ResourceT>
        struct PtrTraits {
            [[nodiscard]] static bool isValid(const ResourceT &item) {
                return item != nullptr;
            }
            [[nodiscard]] static ResourceT empty() {
                return nullptr;
            }
        };
    } // namespace auto_release_impl

    /**
     * Safely manages a handle, pointer, or other resource that requires calling a function
     * on release.
     */
    template<typename ResourceT, typename Traits = auto_release_impl::BasicTraits<ResourceT>>
    class AutoRelease {
        using ResourceType = ResourceT;
        using ReleaseLambda = std::function<void(ResourceType)>;

    protected:
        ReleaseLambda _lambda{};
        ResourceType _resource = Traits::empty();

    public:
        AutoRelease() noexcept = default;

        explicit AutoRelease(const ReleaseLambda &lambda) noexcept : _lambda(lambda) {
            static_assert(std::is_move_assignable_v<ResourceType>);
        }

        AutoRelease(ReleaseLambda lambda, ResourceType resource) noexcept
            : _lambda(std::move(lambda)), _resource(std::move(resource)) {
            static_assert(std::is_move_assignable_v<ResourceType>);
        }

        AutoRelease(const AutoRelease &other) = delete;
        AutoRelease &operator=(const AutoRelease &other) = delete;
        AutoRelease(AutoRelease &&other) noexcept = default;
        AutoRelease &operator=(AutoRelease &&other) noexcept = default;

        ~AutoRelease() noexcept {
            release();
        }

        AutoRelease &operator=(ResourceType resource) noexcept {
            release();
            set(std::move(resource));
            return *this;
        }

        void setRelease(const ReleaseLambda &lambda) noexcept {
            _lambda = lambda;
        }

        void set(ResourceType resource) {
            release();
            _resource = std::move(resource);
        }

        void release() noexcept {
            if(Traits::isValid(_resource)) {
                if(_lambda) {
                    _lambda(std::move(_resource));
                }
            }
            _resource = Traits::empty();
        }

        [[nodiscard]] ResourceType &get() {
            return _resource;
        }

        [[nodiscard]] const ResourceType &get() const {
            return _resource;
        }

        [[nodiscard]] explicit operator bool() const {
            return Traits::isValid(_resource);
        }

        [[nodiscard]] bool operator!() const {
            return !Traits::isValid(_resource);
        }
    };

    /**
     * Version of AutoRelease when it's a pointer
     */
    template<typename PtrT>
    class AutoReleasePtr : public AutoRelease<PtrT *, auto_release_impl::PtrTraits<PtrT *>> {
        //
    public:
        using AutoRelease<PtrT *, auto_release_impl::PtrTraits<PtrT *>>::AutoRelease;
    };
} // namespace util
