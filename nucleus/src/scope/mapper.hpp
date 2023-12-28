#pragma once
#include "data/string_table.hpp"
#include "scope/context.hpp"

namespace scope {
    struct SymbolMapper {
        SymbolMapper() = default;
        SymbolMapper(const SymbolMapper &) = default;
        SymbolMapper(SymbolMapper &&) = default;
        SymbolMapper &operator=(const SymbolMapper &) = default;
        SymbolMapper &operator=(SymbolMapper &&) = default;
        virtual ~SymbolMapper() = default;
        [[nodiscard]] virtual data::Symbol::Partial partial(const data::Symbol &) const = 0;
        [[nodiscard]] virtual data::Symbol apply(data::Symbol::Partial) const = 0;
    };

    class SharedContextMapper : public SymbolMapper, public scope::UsesContext {

    public:
        explicit SharedContextMapper(const UsingContext &context) : scope::UsesContext(context) {
        }
        [[nodiscard]] data::Symbol::Partial partial(const data::Symbol &symbol) const override;
        [[nodiscard]] data::Symbol apply(data::Symbol::Partial partial) const override;
    };

} // namespace scope
