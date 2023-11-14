#pragma once
#include <memory>

namespace scope {

    //
    // Document that a pointer lifetime is global - or global enough that we don't need to worry
    // about ref-counting. The object receiving and using it can assume that the underlying pointer
    // will never go away. The use of the pointer is however constrained - no pointer arithmetic
    // is allowed. We use this fundamentally for data tracked by a context.
    //
    template<typename PtrType>
    class FixedPtr {
        PtrType *_p{nullptr};

        constexpr explicit FixedPtr(PtrType *p) noexcept : _p(p) {
        }

    public:
        constexpr FixedPtr() noexcept = default;
        constexpr FixedPtr(const FixedPtr &) noexcept = default;
        constexpr FixedPtr(FixedPtr &&) noexcept = default;
        constexpr FixedPtr &operator=(const FixedPtr &) noexcept = default;
        constexpr FixedPtr &operator=(FixedPtr &&) noexcept = default;
        ~FixedPtr() noexcept = default;

        constexpr static FixedPtr of(PtrType *p) noexcept {
            return FixedPtr(p);
        }

        PtrType *get() const noexcept {
            return _p;
        }

        PtrType &operator*() const noexcept {
            return *_p;
        }

        PtrType *operator->() const noexcept {
            return _p;
        }

        explicit operator bool() const noexcept {
            return _p != nullptr;
        }

        bool operator!() const noexcept {
            return _p == nullptr;
        }

        void release() noexcept {
            reset();
        }

        void reset() noexcept {
            _p = nullptr;
        }

        void swap(FixedPtr &other) noexcept {
            std::swap(_p, other._p);
        }

        struct Hash {
            using hash_type = std::hash<PtrType *>;
            using is_transparent = void;

            size_t operator()(const FixedPtr &ptr) {
                return hash_type{}(ptr._p);
            }
        };
    };

    template<typename PtrType>
    bool operator==(FixedPtr<PtrType> a, FixedPtr<PtrType> b) {
        return a.get() == b.get();
    }

    template<typename PtrType>
    bool operator!=(FixedPtr<PtrType> a, FixedPtr<PtrType> b) {
        return a.get() != b.get();
    }

} // namespace scope
