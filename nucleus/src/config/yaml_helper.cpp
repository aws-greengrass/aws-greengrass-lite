#include "yaml_helper.hpp"
#include "data/shared_list.hpp"
#include "scope/context_full.hpp"
#include "util/commitable_file.hpp"
#include <memory>

namespace config {
    void YamlReader::read(const std::filesystem::path &path) {
        std::ifstream stream{path};
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        read(stream);
    }

    void YamlReader::read(std::ifstream &stream) {
        //
        // yaml-cpp has a number of flaws, but short of rewriting a YAML parser, is
        // sufficient to get going
        //
        if(!stream.is_open()) {
            throw std::runtime_error("Unable to read config file");
        }
        YAML::Node root = YAML::Load(stream);
        inplaceMap(_target, root);
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlReader::inplaceMap(const std::shared_ptr<Topics> &topics, YAML::Node &node) {
        if(!node.IsMap()) {
            throw std::runtime_error("Expecting a map");
        }
        for(auto i : node) {
            auto key = i.first.as<std::string>();
            inplaceValue(topics, key, i.second);
        }
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlReader::inplaceValue(
        const std::shared_ptr<Topics> &topics, const std::string &key, YAML::Node &node
    ) {
        switch(node.Type()) {
        case YAML::NodeType::Map:
            nestedMapValue(topics, key, node);
            break;
        case YAML::NodeType::Sequence:
        case YAML::NodeType::Scalar:
        case YAML::NodeType::Null:
            inplaceTopicValue(topics, key, rawValue(node));
            break;
        default:
            // ignore anything else
            break;
        }
    }

    // NOLINTNEXTLINE(*-no-recursion)
    data::ValueType YamlReader::rawValue(YAML::Node &node) {
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

    void YamlReader::inplaceTopicValue(
        const std::shared_ptr<Topics> &topics, const std::string &key, const data::ValueType &vt
    ) {
        Topic topic = topics->createTopic(key, _timestamp);
        topic.withNewerValue(_timestamp, vt);
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlReader::nestedMapValue(
        const std::shared_ptr<Topics> &topics, const std::string &key, YAML::Node &node
    ) {
        std::shared_ptr<Topics> nested = topics->createInteriorChild(key, _timestamp);
        inplaceMap(nested, node);
    }

    // NOLINTNEXTLINE(*-no-recursion)
    data::ValueType YamlReader::rawSequenceValue(YAML::Node &node) {
        std::shared_ptr<data::SharedList> newList{std::make_shared<data::SharedList>(_context)};
        int idx = 0;
        for(auto i : node) {
            newList->put(idx++, data::StructElement(rawValue(i)));
        }
        return newList;
    }

    // NOLINTNEXTLINE(*-no-recursion)
    data::ValueType YamlReader::rawMapValue(YAML::Node &node) {
        std::shared_ptr<data::SharedStruct> newMap{std::make_shared<data::SharedStruct>(_context)};
        for(auto i : node) {
            auto key = i.first.as<std::string>();
            newMap->put(key, data::StructElement(rawValue(node)));
        }
        return newMap;
    }

    void YamlHelper::write(
        const std::shared_ptr<scope::Context> &context,
        util::CommitableFile &path,
        const std::shared_ptr<Topics> &node) {
        path.begin(std::ios_base::out | std::ios_base::trunc);
        std::ofstream &stream = path.getStream();
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        write(context, stream, node);
        path.commit();
    }

    void YamlHelper::write(
        const std::shared_ptr<scope::Context> &context,
        std::ofstream &stream,
        const std::shared_ptr<Topics> &node) {
        YAML::Emitter out;
        if(!stream.is_open()) {
            throw std::runtime_error("Unable to write config file");
        }
        serialize(context, out, node);
        stream << out.c_str();
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlHelper::serialize(
        const std::shared_ptr<scope::Context> &context,
        YAML::Emitter &emitter,
        const std::shared_ptr<Topics> &node) {
        emitter << YAML::BeginMap;
        std::vector<Topic> leafs = node->getLeafs();
        for(const auto &i : leafs) {
            emitter << YAML::Key << i.getNameOrd().toString() << YAML::Value;
            serialize(context, emitter, i);
        }
        leafs.clear();
        std::vector<std::shared_ptr<Topics>> subTopics = node->getInteriors();
        for(const auto &i : subTopics) {
            emitter << YAML::Key << i->getName() << YAML::Value;
            serialize(context, emitter, i);
        }
        subTopics.clear();
        emitter << YAML::EndMap;
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
} // namespace config
