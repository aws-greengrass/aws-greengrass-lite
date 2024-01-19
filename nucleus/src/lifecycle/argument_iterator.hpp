#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace lifecycle {
    struct ArgumentIterator {
        const std::vector<std::string> &args;
        std::vector<std::string>::const_iterator &iter;

        ArgumentIterator &operator++() {
            ++iter;
            if(iter == args.end()) {
                throw std::out_of_range("No remaining arguments");
            }
            return *this;
        }

        const std::string &operator*() const noexcept {
            return *iter;
        }
    };

} // namespace lifecycle
