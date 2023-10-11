#pragma once
#include "struct_model.h"

namespace data {

    /**
     * Typical implementation of StructModelBase
     */
    class SharedStruct : public StructModelBase {
    protected:
        std::map<StringOrd, StructElement, StringOrd::CompLess> _elements;
        mutable std::shared_mutex _mutex;

        void rootsCheck(const ContainerModelBase *target) const override;

    public:
        explicit SharedStruct(Environment &environment) : StructModelBase{environment} {
        }

        uint32_t size() const override;
        void put(StringOrd handle, const StructElement &element) override;
        void put(std::string_view sv, const StructElement &element) override;
        bool hasKey(StringOrd handle) const override;

        std::vector<data::StringOrd> getKeys() const override;

        StructElement get(StringOrd handle) const override;
        StructElement get(std::string_view sv) const override;
        std::shared_ptr<StructModelBase> copy() const override;
    };

} // namespace data
