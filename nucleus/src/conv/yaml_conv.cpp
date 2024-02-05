#include "yaml_conv.hpp"
#include "data/shared_buffer.hpp"
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
        auto newList{std::make_shared<data::SharedList>(context())};
        int idx = 0;
        for(auto i : node) {
            newList->put(idx++, data::StructElement(rawValue(i)));
        }
        return newList;
    }

    // NOLINTNEXTLINE(*-no-recursion)
    data::ValueType YamlReaderBase::rawMapValue(YAML::Node &node) {
        auto newMap{std::make_shared<data::SharedStruct>(context())};
        for(auto i : node) {
            auto key = i.first.as<std::string>();
            newMap->put(key, data::StructElement(rawValue(i.second)));
        }
        return newMap;
    }

    void YamlReader::begin(YAML::Node &node) {
        inplaceMap(_target, node);
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlReader::inplaceMap(std::shared_ptr<data::SharedStruct> &data, YAML::Node &node) {
        if(!node.IsMap()) {
            throw std::runtime_error("Expecting a map or sequence");
        }
        for(auto i : node) {
            auto key = util::lower(i.first.as<std::string>());
            inplaceValue(data, key, i.second);
        }
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlReader::inplaceValue(
        std::shared_ptr<data::SharedStruct> &data, const std::string &key, YAML::Node &node) {
        switch(node.Type()) {
            case YAML::NodeType::Map:
                nestedMapValue(data, key, node);
                break;
            case YAML::NodeType::Sequence:
            case YAML::NodeType::Scalar:
            case YAML::NodeType::Null:
                data->put(key, rawValue(node));
                break;
            default:
                // ignore anything else
                break;
        }
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlReader::nestedMapValue(
        std::shared_ptr<data::SharedStruct> &data, const std::string &key, YAML::Node &node) {
        auto child = std::make_shared<data::SharedStruct>(scope::context());
        data->put(key, child);
        inplaceMap(child, node);
    }

    std::shared_ptr<data::SharedBuffer> YamlHelper::serializeToBuffer(
        const scope::UsingContext &context, const std::shared_ptr<data::TrackedObject> &obj) {
        YAML::Emitter emitter;
        serialize(context, emitter, obj);
        auto buffer = std::make_shared<data::SharedBuffer>(context);
        buffer->put(0, data::ConstMemoryView(emitter.c_str(), emitter.size()));
        return buffer;
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlHelper::serialize(
        const scope::UsingContext &context,
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
