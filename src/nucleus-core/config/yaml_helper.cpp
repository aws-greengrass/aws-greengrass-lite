#include "yaml_helper.h"
#include "data/environment.h"
#include "data/shared_list.h"
#include <fstream>
#include <memory>

namespace config {
    void YamlReader::read(const std::filesystem::path &path) {
        //
        // yaml-cpp has a number of flaws, but short of rewriting a YAML parser, is
        // sufficient to get going
        //
        std::ifstream stream{path};
        if(!stream) {
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
        Topic topic = topics->createChild(key, _timestamp);
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
        std::shared_ptr<data::SharedList> newList{std::make_shared<data::SharedList>(_environment)};
        int idx = 0;
        for(auto i : node) {
            newList->put(idx++, data::StructElement(rawValue(i)));
        }
        return newList;
    }

    // NOLINTNEXTLINE(*-no-recursion)
    data::ValueType YamlReader::rawMapValue(YAML::Node &node) {
        std::shared_ptr<data::SharedStruct> newMap{
            std::make_shared<data::SharedStruct>(_environment)};
        for(auto i : node) {
            auto key = i.first.as<std::string>();
            newMap->put(key, data::StructElement(rawValue(node)));
        }
        return newMap;
    }
} // namespace config
