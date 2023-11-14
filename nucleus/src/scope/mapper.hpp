#pragma once
#include "data/string_table.hpp"

namespace data {}

namespace scope {
    class Context;

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

    class SharedContextMapper : public SymbolMapper {
        std::weak_ptr<Context> _context;

    public:
        explicit SharedContextMapper(const std::shared_ptr<Context> &context) : _context(context) {
        }
        [[nodiscard]] Context &context() const;
        [[nodiscard]] data::Symbol::Partial partial(const data::Symbol &symbol) const override;
        [[nodiscard]] data::Symbol apply(data::Symbol::Partial partial) const override;
    };

} // namespace scope