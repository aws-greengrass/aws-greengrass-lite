#pragma once
#include "data/struct_model.hpp"

namespace data {
    // For now, essentially a rename
    class Object final : public StructElement {
    public:
        Object(const Object &) = default;
        Object(Object &&) = default;
        Object &operator=(const Object &el) = default;
        Object &operator=(Object &&el) noexcept = default;
        ~Object() final = default;

        Object() : StructElement() {
        }

        template<typename T>
        // NOLINTNEXTLINE(*-explicit-constructor)
        Object(const T &v) : StructElement(v) {
        }
    };
} // namespace data
