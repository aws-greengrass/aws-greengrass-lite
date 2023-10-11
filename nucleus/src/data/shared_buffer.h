#pragma once
#include "struct_model.h"

namespace data {

    typedef uint8_t Byte;
    typedef util::Span<Byte, uint32_t> MemoryView;
    typedef util::Span<const Byte, uint32_t> ConstMemoryView;

    //
    // A byte-buffer that can be shared between multiple modules
    //
    class SharedBuffer : public ContainerModelBase {
    protected:
        static constexpr uint32_t MAX_BUFFER_SIZE{0x100000};

        std::vector<uint8_t> _buffer;
        mutable std::shared_mutex _mutex;

        void rootsCheck(const ContainerModelBase *target) const override {
            // no-op
        }

        void putOrInsert(int32_t idx, ConstMemoryView bytes, bool insert);

    public:
        explicit SharedBuffer(Environment &environment) : ContainerModelBase{environment} {
        }

        void put(int32_t idx, ConstMemoryView bytes);
        void insert(int32_t idx, ConstMemoryView bytes);
        uint32_t size() const override;
        void resize(uint32_t newSize);
        uint32_t get(int idx, MemoryView bytes) const;
    };
} // namespace data
