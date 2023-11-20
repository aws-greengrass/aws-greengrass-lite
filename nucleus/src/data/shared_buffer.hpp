#pragma once
#include "struct_model.hpp"

namespace data {
    using MemoryView = util::Span<char, uint32_t>;
    using ConstMemoryView = util::Span<const char, uint32_t>;

    //
    // A byte-buffer that can be shared between multiple modules
    //
    class SharedBuffer : public ContainerModelBase {
    protected:
        static constexpr uint32_t MAX_BUFFER_SIZE{0x100000};

        std::vector<char> _buffer;
        mutable std::shared_mutex _mutex;

        void rootsCheck(const ContainerModelBase *target) const override {
            // no-op
        }

        void putOrInsert(int32_t idx, ConstMemoryView bytes, bool insert);

    public:
        explicit SharedBuffer(const std::shared_ptr<scope::Context> &context)
            : ContainerModelBase{context} {
        }

        void put(int32_t idx, ConstMemoryView bytes);
        void insert(int32_t idx, ConstMemoryView bytes);
        uint32_t size() const override;
        void resize(uint32_t newSize);
        uint32_t get(int idx, MemoryView bytes) const;
    };
} // namespace data
