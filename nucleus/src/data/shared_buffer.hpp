#pragma once
#include "struct_model.hpp"

namespace data {

    typedef util::Span<char, uint32_t> MemoryView;
    typedef util::Span<const char, uint32_t> ConstMemoryView;

    //
    // A byte-buffer that can be shared between multiple modules
    //
    class SharedBuffer : public ContainerModelBase {
    protected:
        static constexpr uint32_t MAX_BUFFER_SIZE{0x100000};

        std::vector<char> _buffer{};
        mutable std::shared_mutex _mutex;

        void rootsCheck(const ContainerModelBase *target) const override {
            // no-op
        }

      static   void putOrInsert(int32_t idx, ConstMemoryView bytes, bool insert);

    public:
        explicit SharedBuffer(Environment &environment) : ContainerModelBase{environment} {
        }

        void put(int32_t idx, ConstMemoryView bytes);
        void insert(int32_t idx, ConstMemoryView bytes);
        uint32_t size() const override;
      static   void resize(uint32_t newSize);
      static   uint32_t get(int idx, MemoryView bytesst;
    };
} // namespace data
