#pragma once
#include <data/struct_model.hpp>
#include <memory>

namespace conv {

    class Archive {
        bool _ignoreKeyCase = false;

    protected:
        std::unordered_map<std::string, data::StructElement> _kv;

    public:
        void setIgnoreKeyCase(bool ignoreCase = true) {
            _ignoreKeyCase = ignoreCase;
        }

        Archive &operator=(Archive const &) = delete;

        [[nodiscard]] data::StructElement get(const std::string &name) {
            std::string key;
            if(_ignoreKeyCase) {
                key = util::lower(name);
            } else {
                key = name;
            }
            if(_kv.find(key) != _kv.cend()) {
                return _kv.at(key);
            } else {
                throw std::runtime_error("Unknown key");
            }
        }

        [[nodiscard]] bool compareKeys( std::string key, std::string name) {
            if(_ignoreKeyCase) {
                key = util::lower(key);
                name = util::lower(name);
            }
            return key == name;
        }

        //
        //        virtual void apply(std::string &data) {
        //            throw std::logic_error("Not expected");
        //        }
        //
        //        virtual void apply(uint64_t &data) {
        //            throw std::logic_error("Not expected");
        //        }
        //
        //        virtual void apply(std::shared_ptr<data::StructModelBase> &objectStruct) {
        //            throw std::logic_error("Not expected");
        //        }
        //
        //        virtual void applyAllKeys(std::function<void(std::string_view,
        //        data::StructElement)> &func) {
        //            throw std::logic_error("Not expected");
        //        }
    };

    class Serializable {
    public:
        Serializable() = default;
        virtual ~Serializable() = default;
        Serializable(const Serializable &other) = default;
        Serializable(Serializable &&) = default;
        Serializable &operator=(const Serializable &other) = default;
        Serializable &operator=(Serializable &&) = default;
    };
} // namespace conv
