#pragma once

#include "argument_iterator.hpp"
#include <greengrass_traits.hpp>
#include <string_util.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace lifecycle {
    class CommandLine;

    // Lint reasoning: arguments are not referred to by-reference to base class.
    // Arguments must be trivially-destructible to be stored at-compile-time in a tuple
    // NOLINTNEXTLINE(*-virtual-class-destructor)
    class Argument {
    private:
        constexpr static const std::string_view _optionMarker = "-";
        constexpr static const std::string_view _longOptionMarker = "--";
        std::string_view _option;
        std::string_view _longOption;
        std::string_view _description;

    protected:
        [[nodiscard]] constexpr std::string_view option() const noexcept {
            return _option;
        }

        [[nodiscard]] constexpr std::string_view longOption() const noexcept {
            return _option;
        }

        [[nodiscard]] constexpr std::string_view description() const noexcept {
            return _option;
        }

        [[nodiscard]] bool isMatch(std::string_view argString) const {
            if(util::startsWith(argString, _longOptionMarker)) {
                return util::lower(argString.substr(2)) == _longOption;
            } else if(util::startsWith(argString, _optionMarker)) {
                return util::lower(argString.substr(1)) == _option;
            }
            return false;
        }

        constexpr Argument(
            std::string_view option,
            std::string_view longOption,
            std::string_view description) noexcept
            : _option(option), _longOption(longOption), _description(description) {
        }

    public:
        template<class... Arguments>
        static void printHelp(Arguments &&...args) {
            (args.printDescription(), ...);
        }

        template<class... Arguments>
        [[nodiscard]] static bool processArg(
            CommandLine &cli, ArgumentIterator &argIter, Arguments &&...args) {
            // tests each argument parser in the parameter pack against the current argument.
            // Terminates and returns true on the first match.
            return (args.process(cli, argIter) || ...);
        }

        void printDescription() const {
            std::cout << _optionMarker << _option << "\t" << _longOptionMarker << _longOption
                      << " : " << _description << '\n';
        }

        virtual bool process(CommandLine &, ArgumentIterator &i) const = 0;
    };

    template<class HandlerFn>
    class ArgumentFlag final : public Argument {
    private:
        HandlerFn _handler;

    public:
        template<typename... A>
        explicit constexpr ArgumentFlag(HandlerFn &&handler, A &&...a) noexcept(
            std::is_nothrow_move_assignable_v<HandlerFn>)
            : Argument(std::forward<A>(a)...), _handler(std::move(handler)) {
        }

        bool process(CommandLine &cli, ArgumentIterator &i) const override {
            if(isMatch(*i)) {
                _handler(cli);
                return true;
            }
            return false;
        }
    };

    template<class T, class HandlerFn>
    class ArgumentValue final : public Argument {
    protected:
        [[nodiscard]] T extractValue(const std::string &val) const {
            if constexpr(std::is_same_v<T, int>) {
                return std::stoi(val);
            } else if constexpr(std::is_same_v<T, std::string>) {
                return val;
            } else if constexpr(std::is_same_v<T, std::string_view>) {
                return std::string_view{val};
            } else {
                static_assert(util::traits::always_false_v<T>, "Extraction not implemented");
            }
        }

        HandlerFn _handler;

    public:
        template<typename... A>
        explicit constexpr ArgumentValue(HandlerFn &&handler, A &&...a) noexcept(
            std::is_nothrow_move_constructible_v<HandlerFn>)
            : Argument(std::forward<A>(a)...), _handler(std::move(handler)) {
        }

        /** process the i'th string in args and determine if this is a match
         return true if it is a match and false if it is not a match */
        bool process(CommandLine &cli, ArgumentIterator &i) const override {
            if(isMatch(*i)) {
                try {
                    ++i;
                    _handler(cli, extractValue(*i));
                    return true;
                } catch(const std::invalid_argument &e) {
                    std::cout << "invalid argument for " << longOption() << std::endl;
                } catch(const std::out_of_range &e) {
                    std::cout << "missing argument for " << longOption() << std::endl;
                } catch(...) {
                    std::cout << "unexpected exception" << std::endl;
                }
            }
            return false;
        };
    };

    template<class HandlerFn, class... A>
    constexpr auto makeArgumentFlag(HandlerFn &&fn, A &&...a) noexcept {
        return ArgumentFlag<HandlerFn>{std::forward<HandlerFn>(fn), std::forward<A>(a)...};
    }

    template<class T, class HandlerFn, class... A>
    constexpr auto makeArgumentValue(HandlerFn &&fn, A &&...a) noexcept {
        return ArgumentValue<T, HandlerFn>{std::forward<HandlerFn>(fn), std::forward<A>(a)...};
    }
} // namespace lifecycle
