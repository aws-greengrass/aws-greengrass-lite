#pragma once
#include "struct_model.hpp"
#include <memory>

namespace conv {

    class Archive {
    protected:
        bool _ignoreKeyCase = false;

    public:
        void setIgnoreKeyCase(bool ignoreCase = true) {
            _ignoreKeyCase = ignoreCase;
        }
        Archive() = default;
        virtual ~Archive() = default;
        Archive(const Archive &other) = default;
        Archive(Archive &&) = default;
        Archive &operator=(const Archive &other) = delete;
        Archive &operator=(Archive &&) = delete;
    };

    class Serializable {
    protected:
        bool _ignoreKeyCase = false;

    public:
        Serializable() = default;
        virtual ~Serializable() = default;
        Serializable(const Serializable &other) = default;
        Serializable(Serializable &&) = default;
        Serializable &operator=(const Serializable &other) = default;
        Serializable &operator=(Serializable &&) = default;

        void setIgnoreKeyCase(bool ignoreCase = true) {
            _ignoreKeyCase = ignoreCase;
        }
    };
} // namespace conv
