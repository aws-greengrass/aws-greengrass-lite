#pragma once

namespace config {
    class Validator : public Watcher {
    public:
        Validator() = default;
        ~Validator() = default;

        virtual Validator *getValidator();
        virtual std::string validate(std::string newValue, std::string oldValue);
    };
} // namespace config
