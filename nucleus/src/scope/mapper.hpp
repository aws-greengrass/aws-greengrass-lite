#pragma once

namespace data {
    class PartialHandle;
    class Symbol;
} // namespace data

namespace scope {
    class Context;
    data::Symbol apply(scope::Context &context, const data::PartialHandle &partial);
    data::PartialHandle partial(scope::Context &context, const data::Symbol &symbol);
} // namespace scope
