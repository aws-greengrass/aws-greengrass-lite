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
        using BadCastError = errors::InvalidBufferError;

        explicit SharedBuffer(const scope::UsingContext &context) : ContainerModelBase{context} {
        }

        void put(int32_t idx, ConstMemoryView bytes);
        void insert(int32_t idx, ConstMemoryView bytes);
        uint32_t size() const override;
        void resize(uint32_t newSize);
        uint32_t get(int idx, MemoryView bytes) const;
        std::shared_ptr<ContainerModelBase> parseJson();
        std::shared_ptr<ContainerModelBase> parseYaml();
        void write(std::ostream &stream) const;
    };

} // namespace data

inline static std::ostream &operator<<(std::ostream &os, const data::SharedBuffer &buffer) {
    buffer.write(os);
    return os;
}

inline static std::ostream &operator<<(
    std::ostream &os, const std::shared_ptr<data::SharedBuffer> &buffer) {
    buffer->write(os);
    return os;
}
