#pragma once

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <list>
#include <optional>
#include <string>

namespace lifecycle {
    class argument {
    private:
    protected:
        const std::string _option;
        const std::string _longOption;
        const std::string _description;

        [[nodiscard]] bool isMatch(const std::string &argString) const {
            bool returnValue = false;
            std::string a = argString;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if(a == _option || a == _longOption) {
                returnValue = true;
            }
            return returnValue;
        }

        argument(
            const std::string_view option,
            const std::string_view longOption,
            const std::string_view description)
            : _option(std::string("-") + std::string(option)),
              _longOption(std::string("--") + std::string(longOption)), _description(description) {
        }

    public:
        virtual ~argument() = default;

        argument(argument &&) = delete;
        argument &operator=(const argument &rhs) = delete;
        argument(const argument &) = delete;
        argument &operator=(argument &&) = delete;

        [[nodiscard]] std::string getDescription() const {
            std::string descriptionString;
            descriptionString = _option + "\t" + _longOption + " : " + _description;
            return descriptionString;
        }

        virtual bool process(std::vector<std::string>::const_iterator &i) const = 0;
    };

    class argumentFlag : public argument {
    private:
        std::function<void(void)> _handler;
        ;

    public:
        argumentFlag(
            const std::string_view flag,
            const std::string_view name,
            const std::string_view description,
            const std::function<void(void)> &handler)
            : argument(flag, name, description), _handler(handler){};

        bool process(std::vector<std::string>::const_iterator &i) const override {
            if(isMatch(*i)) {
                _handler();
                return true;
            }
            return false;
        }
    };

    template<class T>
    class argumentValue : public argument {
    protected:
        T _extractValue(const std::string &val) const;

        std::function<void(T)> _handler;

    public:
        argumentValue(
            const std::string_view flag,
            const std::string_view name,
            const std::string_view description,
            const std::function<void(T)> handler)
            : argument(flag, name, description), _handler(handler){};

        /** process the i'th string in args and determine if this is a match
         return true if it is a match and false if it is not a match */
        bool process(std::vector<std::string>::const_iterator &i) const override {
            if(isMatch(*i)) {
                try {
                    i++;
                    _handler(_extractValue(*i));
                    return true;
                } catch(const std::invalid_argument &e) {
                    std::cout << "invalid argument for " << _longOption << std::endl;
                } catch(...) {
                    std::cout << "unexpected exception" << std::endl;
                }
            }
            return false;
        };
    };

    template<>
    inline std::string argumentValue<std::string>::_extractValue(const std::string &val) const {
        if(val.empty())
            throw(std::invalid_argument("Missing a parameter"));
        return val;
    }

    template<>
    inline int argumentValue<int>::_extractValue(const std::string &val) const {
        return std::stoi(val);
    }
}; // namespace lifecycle