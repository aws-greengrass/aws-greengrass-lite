#pragma once

#include <algorithm>
#include <iostream>
#include <iterator>
#include <list>
#include <optional>
#include <string>
#include <filesystem>

namespace lifecycle {
    class argument {
    private:
        argument &operator=(const argument &rhs) = delete;

    protected:
        const std::string _flag;
        const std::string _name;
        const std::string _description;

        argument(const std::string_view flag, const std::string_view name, const std::string_view description)
                : _flag(flag), _name(name),
                  _description(description) {
        }

    public:
        virtual ~argument() = default;

        const std::string getHelp() const {
            std::string helpString;
            helpString = "-" + _flag + "\t" + "--" + _name + " : " + _description;
            return helpString;
        }

        virtual bool process(std::vector<std::string>::const_iterator &i) = 0;
    };

    class argumentFlag : public argument {
    private:
        std::function<void(void)> _handler;;
    public:
        argumentFlag(
                const std::string_view flag,
                const std::string_view name,
                const std::string_view description,
                const std::function<void(void)> handler
        ) : argument(flag, name, description), _handler(handler) {};

        bool process(std::vector<std::string>::const_iterator &i) override {
            std::string a = *i;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a == "-" + _flag || a == "--" + _name) {
                _handler();
                return true;
            }
            return false;
        }
    };

    template<class T>
    class argumentValue : public argument {
    protected:
        T _value;

        void _extractValue(const std::string &val);

        std::function<void(T)> _handler;
    public:
        argumentValue(
                const std::string_view flag,
                const std::string_view name,
                const std::string_view description,
                const std::function<void(T)> handler) : argument(flag, name, description), _handler(handler) {};

        /** process the i'th string in args and determine if this is a match
         return true if it is a match and false if it is not a match */
        bool process(std::vector<std::string>::const_iterator &i) override {
            std::string a = *i;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a == "-" + _flag || a == "--" + _name) {
                try {
                    i++;
                    _extractValue(*i);
                    _handler(_value);
                    return true;
                } catch (const std::invalid_argument &e) {
                    std::cout << "invalid argument for " << _name << std::endl;
                } catch (...) {
                    std::cout << "unexpected exception" << std::endl;
                }
            }
            return false;
        };

        T getValue() {
            return _value;
        };
    };

    template<>
    inline void argumentValue<std::string>::_extractValue(const std::string &val) {
        if (val.empty())
            throw (std::invalid_argument("Missing a parameter"));
        _value = val;
    }

    template<>
    inline void argumentValue<int>::_extractValue(const std::string &val) {
        _value = std::stoi(val);
    }
}; // namespace lifecycle