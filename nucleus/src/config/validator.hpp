#pragma once

namespace config {
    class Validator : public Watcher {
    public:
        Validator() = default;
        ~Validator() = default;
        
        virtual std::string validate(std::string newValue, std::string oldValue);
    };
} // namespace config
