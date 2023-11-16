#pragma once
#include "scope/mapper.hpp"
#include <functional>

namespace scope {
    class Context;
}

namespace data {
    class SymbolMapper;
    //
    // Recurring pattern of having a table of symbols - As currently implemented, does not
    // provide a map facade, but does provide a set of helper methods for common operators
    //

    template<typename T>
    class SymbolValueMap {
    private:
        using ValueMap = std::map<data::Symbol::Partial, T, data::Symbol::Partial::CompLess>;

    public:
        explicit SymbolValueMap(scope::SymbolMapper &mapper) : _mapper(mapper) {
        }
        SymbolValueMap(const SymbolValueMap &other) = delete;
        SymbolValueMap(SymbolValueMap &&other) = delete;
        SymbolValueMap &operator=(SymbolValueMap &&other) = delete;
        ~SymbolValueMap() = default;

        ValueMap &get() {
            return _values;
        }

        const ValueMap &get() const {
            return _values;
        }

        T &at(const Symbol &k) {
            return _values.at(partial(k));
        }

        typename ValueMap::iterator find(const Symbol &k) {
            return _values.find(partial(k));
        }

        typename ValueMap::const_iterator find(const Symbol &k) const {
            return _values.find(partial(k));
        }

        typename ValueMap::iterator begin() {
            return _values.begin();
        }

        typename ValueMap::iterator end() {
            return _values.end();
        }

        typename ValueMap::const_iterator begin() const {
            return _values.begin();
        }

        typename ValueMap::const_iterator end() const {
            return _values.end();
        }

        typename ValueMap::const_iterator cbegin() const {
            return _values.cbegin();
        }

        typename ValueMap::const_iterator cend() const {
            return _values.cend();
        }

        template<typename M>
        auto insert_or_assign(const Symbol &symbol, M &&obj) {
            return _values.insert_or_assign(partial(symbol), obj);
        }

        template<typename... Args>
        auto emplace(const Symbol &symbol, Args &&...args) {
            return _values.emplace(partial(symbol), std::forward<Args>(args)...);
        }

        auto erase(const Symbol &symbol) {
            return _values.erase(partial(symbol));
        }

        template<typename I>
        auto erase(I pos) {
            return _values.erase(pos);
        }

        auto size() const {
            return _values.size();
        }

        [[nodiscard]] Symbol::Partial partial(const Symbol &symbol) const {
            return _mapper.partial(symbol);
        }

        [[nodiscard]] Symbol apply(const Symbol::Partial &symbol) const {
            return _mapper.apply(symbol);
        }

        SymbolValueMap &operator=(const SymbolValueMap &other) {
            _values = other._values;
            return *this;
        }

    private:
        scope::SymbolMapper &_mapper;
        ValueMap _values;
    };

} // namespace data
