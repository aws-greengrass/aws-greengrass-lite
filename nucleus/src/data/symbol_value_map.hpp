#pragma once
#include "scope/context.hpp"
#include "scope/mapper.hpp"
#include <functional>

namespace data {
    //
    // Recurring pattern of having a table of symbols - As currently implemented, does not
    // provide a map facade, but does provide a set of helper methods for common operators
    //

    template<typename T>
    class SymbolValueMap : scope::UsesContext {
    private:
        using ValueMap = std::map<data::Symbol::Partial, T, data::Symbol::Partial::CompLess>;

    public:
        using iterator = typename ValueMap::iterator;
        using const_iterator = typename ValueMap::const_iterator;

        SymbolValueMap(const SymbolValueMap &) = delete;
        SymbolValueMap(SymbolValueMap &&) noexcept = default;
        SymbolValueMap &operator=(SymbolValueMap &&) noexcept = default;
        SymbolValueMap &operator=(const SymbolValueMap &lhs) {
            _values = lhs._values;
            return *this;
        }
        ~SymbolValueMap() noexcept = default;

        using scope::UsesContext::UsesContext;

        ValueMap &get() & noexcept {
            return _values;
        }

        const ValueMap &get() const & noexcept {
            return _values;
        }

        T &at(const Symbol &k) {
            return _values.at(partial(k));
        }

        iterator find(const Symbol &k) {
            return _values.find(partial(k));
        }

        const_iterator find(const Symbol &k) const {
            return _values.find(partial(k));
        }

        iterator begin() noexcept {
            return _values.begin();
        }

        iterator end() noexcept {
            return _values.end();
        }

        const_iterator begin() const noexcept {
            return _values.begin();
        }

        const_iterator end() const noexcept {
            return _values.end();
        }

        const_iterator cbegin() const noexcept {
            return _values.cbegin();
        }

        const_iterator cend() const noexcept {
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

        auto erase(iterator pos) {
            return _values.erase(pos);
        }

        auto erase(const_iterator pos) {
            return _values.erase(pos);
        }

        auto erase(const T &key) {
            return _values.erase(key);
        }

        auto erase(const_iterator first, const_iterator last) {
            return _values.erase(first, last);
        }

        auto size() const noexcept {
            return _values.size();
        }

        auto empty() const noexcept {
            return _values.empty();
        }

        [[nodiscard]] Symbol::Partial partial(const Symbol &symbol) const {
            return scope::partial(*context(), symbol);
        }

        [[nodiscard]] Symbol apply(const Symbol::Partial &partial) const {
            return scope::apply(*context(), partial);
        }

    private:
        ValueMap _values;
    };

} // namespace data
