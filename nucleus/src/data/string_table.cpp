#include "string_table.hpp"
#include "scope/context_full.hpp"

namespace data {

    void data::SymbolInit::init(const std::shared_ptr<scope::Context> &context) const {
        // 'const' is a hack to make this work with initializer_list below
        // actually it does behave as a const, so it's not that big of a hack
        _symbol = context->symbols().intern(_string);
    }

    void SymbolInit::init(
        const std::shared_ptr<scope::Context> &context,
        std::initializer_list<const SymbolInit *> list) {
        for(auto i : list) {
            i->init(context);
        }
    }

    SymbolTable::Buffer::Buffer() {
        // Special case empty string, avoids special casing this size in push.
        // Throw a single character into string buffer, but use it for empty strings.
        _strings.reserve(CHAR_CAPACITY_SPARE);
        _spans.reserve(SPAN_CAPACITY_SPARE);
        _strings.emplace_back(0); // place a single (unused) byte in buffer
        _spans.emplace_back(0, 0);
        assert(_spans.size() == 1);
    }

    SymbolTable::SymbolTable() {
        // Special case empty string, avoids special casing this size in push.
        auto empty = Buffer::empty();
        _lookup.emplace(_buffer.getSpan(empty), empty);
    }

    Symbol SymbolTable::intern(std::string_view str) {
        Symbol sym = testAndGetSymbol(str); // optimistic using shared lock
        if(sym.isNull()) {
            std::unique_lock guard(_mutex);
            auto i = _lookup.find(str);
            if(i == _lookup.end()) {
                auto partial = _buffer.push(str);
                _lookup.emplace(_buffer.getSpan(partial), partial);
                return applyUnchecked(partial);
            } else {
                // this path handles a race condition
                return applyUnchecked(i->second);
            }
        } else {
            return sym;
        }
    }

    Symbol::Partial SymbolTable::Buffer::empty() {
        return symbolOf(EMPTY_LENGTH_INDEX);
    }

    Symbol::Partial SymbolTable::Buffer::push(std::string_view &source) {
        uint32_t bufferIndex = _strings.size();
        uint32_t currCapacity = _strings.capacity();
        if(currCapacity < bufferIndex + source.length()) {
            uint32_t newCapacity = bufferIndex + source.length() + CHAR_CAPACITY_SPARE;
            _strings.reserve(newCapacity);
        }
        _strings.insert(_strings.end(), source.begin(), source.end());
        uint32_t spanIndex = _spans.size();
        uint32_t spanCapacity = _spans.capacity();
        if(spanCapacity <= spanIndex) {
            uint32_t newCapacity = spanIndex + SPAN_CAPACITY_SPARE;
            _spans.reserve(newCapacity);
        }
        _spans.emplace_back(bufferIndex, source.length());
        return symbolOf(spanIndex);
    }
} // namespace data
