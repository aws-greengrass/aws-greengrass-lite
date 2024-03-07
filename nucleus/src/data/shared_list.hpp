#pragma once
#include "struct_model.hpp"

namespace data {
    /**
     * Typical implementation of ListModelBase
     */
    class SharedList : public ListModelBase {
    protected:
        static constexpr uint32_t MAX_LIST_SIZE{0x10000};

        std::vector<StructElement> _elements;
        mutable std::shared_mutex _mutex;

        void rootsCheck(const ContainerModelBase *target) const override;

    public:
        using BadCastError = errors::InvalidListError;

        explicit SharedList(const scope::UsingContext &context) : ListModelBase{context} {
        }

        void reserve(size_t size) {
            _elements.reserve(size);
        }

        void push(const StructElement &element);
        void put(int32_t idx, const StructElement &element) override;
        void insert(int32_t idx, const StructElement &element) override;
        uint32_t size() const override;
        StructElement get(int idx) const override;
        std::shared_ptr<ListModelBase> copy() const override;
    };

    template<>
    ListModelBase *Archive::initSharedPtr(std::shared_ptr<ListModelBase> &ptr);
    template<>
    SharedList *Archive::initSharedPtr(std::shared_ptr<SharedList> &ptr);
} // namespace data
