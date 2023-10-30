#pragma once
#include "struct_model.hpp"

namespace data {

    /**
     * Typical implementation of StructModelBase
     */
    class SharedStruct : public StructModelBase {
    protected:
        std::map<StringOrd, StructElement, StringOrd::CompLess> _elements{};
        mutable std::shared_mutex _mutex;

        void rootsCheck(const ContainerModelBase *target) const override;

    public:
        explicit SharedStruct(Environment &environment) : StructModelBase{environment} {
        }

        uint32_t size() const override;
        void putImpl(StringOrd handle, const StructElement &element) override;
        bool hasKeyImpl(StringOrd handle) const override;
        std::vector<data::StringOrd> getKeys() const override;
        StructElement getImpl(StringOrd handle) const override;
        std::shared_ptr<StructModelBase> copy() const override;
    };

} // namespace data
