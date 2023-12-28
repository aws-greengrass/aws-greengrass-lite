#pragma once

#include <algorithm>
#include <iostream>
#include <iterator>
#include <list>
#include <optional>
#include <string>

namespace lifecycle {
    class argument {
    private:
        argument &operator=(const argument &rhs);

    protected:
        bool _inUse;
        const std::string _flag;
        const std::string _name;
        const std::string _description;
        argument(const std::string &flag, const std::string &name, const std::string &description)
            : _inUse(false), _flag(flag), _name(name), _description(description) {
        }

    public:
        const std::string &getFlag() const {
            return _flag;
        }
        const std::string &getName() const {
            return _name;
        }
        const std::string &getDescription() const {
            return _description;
        }
        bool isUsed() {
            return _inUse;
        }
        virtual bool process(int &i, const std::vector<std::string> &args) = 0;
        inline void addToList(std::list<argument *> &argumentList) const {
            argumentList.push_front(const_cast<argument *>(this));
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
            const std::string &flag,
            const std::string &name,
            const std::string &description,
            std::function<void(T)> handler)
            : argument(flag, name, description), _handler(handler){};

        /** process the i'th string in args and determine if this is a match
         return true if it is a match and false if it is not a match */
        bool process(int &i, const std::vector<std::string> &args) {
            std::string a = args[i];
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);

            if(a == "-" + _flag || a == "--" + _name) {
                i++;
                try {
                    _extractValue(args[i]);
                    _inUse = true;
                    _handler(_value);
                    return true;
                } catch(const std::invalid_argument &e) {
                    std::cout << "invalid argument for " << _name << std::endl;
                } catch(...) {
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
        if(val.empty())
            throw(std::invalid_argument("Missing a parameter"));
        _value = val;
    }

    template<>
    inline void argumentValue<int>::_extractValue(const std::string &val) {
        _value = std::stoi(val);
    }
}; // namespace lifecycle