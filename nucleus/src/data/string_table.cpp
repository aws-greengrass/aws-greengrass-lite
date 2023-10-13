#include "string_table.hpp"
#include "environment.hpp"

namespace data {
    void data::StringOrdInit::init(Environment &environment) const {
        // 'const' is a hack to make this work with intializer_list below
        // actually it does behave as a const, so it's not that big of a hack
        _ord = environment.stringTable.getOrCreateOrd(_string);
    }

    void StringOrdInit::init(Environment &environment, std::initializer_list<StringOrdInit> list) {
        for(auto &i : list) {
            i.init(environment);
        }
    }

} // namespace data
