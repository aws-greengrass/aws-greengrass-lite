#pragma once
#include "scope/mapper.hpp"
#include "struct_model.hpp"
#include "symbol_value_map.hpp"

namespace data {

    /**
     * Typical implementation of StructModelBase
     */
    class SharedStruct : public StructModelBase {
    private:
        scope::SharedContextMapper _symbolMapper;

    protected:
        SymbolValueMap<StructElement> _elements{_symbolMapper};
        mutable std::shared_mutex _mutex;

        void rootsCheck(const ContainerModelBase *target) const override;

    public:
        using BadCastError = errors::InvalidStructError;

        explicit SharedStruct(const std::shared_ptr<scope::Context> &context)
            : StructModelBase(context), _symbolMapper(context) {
        }

        uint32_t size() const override;
        void putImpl(Symbol symbol, const StructElement &element) override;
        bool hasKeyImpl(Symbol symbol) const override;
        std::vector<data::Symbol> getKeys() const override;
        StructElement getImpl(Symbol symbol) const override;
        std::shared_ptr<StructModelBase> copy() const override;
    };

} // namespace data
