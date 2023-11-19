#include "string_table.hpp"
#include "data/data_util.hpp"
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

} // namespace data
