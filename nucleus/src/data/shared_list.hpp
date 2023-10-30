#pragma once
#include "struct_model.hpp"

namespace data {
    /**
     * Typical implementation of ListModelBase
     */
    class SharedList : public ListModelBase {
    protected:
        static constexpr uint32_t MAX_LIST_SIZE{0x10000};

        std::vector<StructElement> _elements{};
        mutable std::shared_mutex _mutex;

        void rootsCheck(const ContainerModelBase *target) const override;

    public:
        explicit SharedList(Environment &environment) : ListModelBase{environment} {
        }

        void put(int32_t idx, const StructElement &element) override;
        void insert(int32_t idx, const StructElement &element) override;
        uint32_t size() const override;
        StructElement get(int idx) const override;
        std::shared_ptr<ListModelBase> copy() const override;
    };

} // namespace data
