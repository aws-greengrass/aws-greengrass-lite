#include "yaml_conv.hpp"
#include "data/shared_list.hpp"
#include "scope/context_full.hpp"
#include "util/commitable_file.hpp"
#include <memory>

namespace conv {
    void YamlReaderBase::read(const std::filesystem::path &path) {
        std::ifstream stream{path};
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        if(!stream.is_open()) {
            throw std::runtime_error("Unable to read config file");
        }
        read(stream);
    }

    void YamlReaderBase::read(std::istream &stream) {
        //
        // yaml-cpp has a number of flaws, but short of rewriting a YAML parser, is
        // sufficient to get going
        //
        YAML::Node root = YAML::Load(stream);
        begin(root);
    }

    // NOLINTNEXTLINE(*-no-recursion)
    data::ValueType YamlReaderBase::rawValue(YAML::Node &node) {
        switch(node.Type()) {
            case YAML::NodeType::Map:
                return rawMapValue(node);
            case YAML::NodeType::Sequence:
                return rawSequenceValue(node);
            case YAML::NodeType::Scalar:
                return node.as<std::string>();
            default:
                break;
        }
        return {};
    }

    // NOLINTNEXTLINE(*-no-recursion)
    data::ValueType YamlReaderBase::rawSequenceValue(YAML::Node &node) {
        std::shared_ptr<data::SharedList> newList{std::make_shared<data::SharedList>(_context)};
        int idx = 0;
        for(auto i : node) {
            newList->put(idx++, data::StructElement(rawValue(i)));
        }
        return newList;
    }

    // NOLINTNEXTLINE(*-no-recursion)
    data::ValueType YamlReaderBase::rawMapValue(YAML::Node &node) {
        std::shared_ptr<data::SharedStruct> newMap{std::make_shared<data::SharedStruct>(_context)};
        for(auto i : node) {
            auto key = i.first.as<std::string>();
            newMap->put(key, data::StructElement(rawValue(node)));
        }
        return newMap;
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlHelper::serialize(
        const std::shared_ptr<scope::Context> &context,
        YAML::Emitter &emitter,
        const data::StructElement &value) {
        switch(value.getType()) {
            case data::ValueTypes::NONE:
                emitter << YAML::Null;
                break;
            case data::ValueTypes::BOOL:
                emitter << value.getBool();
                break;
            case data::ValueTypes::INT:
                emitter << value.getInt();
                break;
            case data::ValueTypes::DOUBLE:
                emitter << value.getDouble();
                break;
            case data::ValueTypes::OBJECT:
                if(value.isType<data::ListModelBase>()) {
                    std::shared_ptr<data::ListModelBase> list =
                        value.castObject<data::ListModelBase>()->copy();
                    auto size = static_cast<int32_t>(list->size());
                    emitter << YAML::BeginSeq;
                    for(int32_t idx = 0; idx < size; idx++) {
                        serialize(context, emitter, list->get(idx));
                    }
                    emitter << YAML::EndSeq;
                } else if(value.isType<data::StructModelBase>()) {
                    std::shared_ptr<data::StructModelBase> s =
                        value.castObject<data::StructModelBase>()->copy();
                    std::vector<data::Symbol> keys = s->getKeys();
                    emitter << YAML::BeginMap;
                    for(const auto &i : keys) {
                        std::string k = i.toString();
                        emitter << YAML::Key << k.c_str() << YAML::Value;
                        serialize(context, emitter, s->get(i));
                    }
                    emitter << YAML::EndMap;
                } else {
                    // Ignore objects that cannot be serialized
                }
                break;
            default:
                emitter << value.getString().c_str();
                break;
        }
    }
} // namespace conv
