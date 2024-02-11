#include "yaml_serializer.hpp"
#include "scope/context_full.hpp"

namespace conv {

//    data::StructElement YamlMapDeserializer::save(YAML::Node &node) {
//        if(!node.IsMap()) {
//            throw std::runtime_error("Expecting a map");
//        }
//        auto target = std::make_shared<data::SharedStruct>(scope::context());
//        for(auto i : node) {
//            auto key = util::lower(i.first.as<std::string>());
//            target->putImpl(data::SymbolInit{key}, decode(i.second));
//        }
//        return target;
//    }
//
//    data::StructElement YamlMapDeserializer::decode(YAML::Node &node) const {
//        switch(node.Type()) {
//            case YAML::NodeType::Map: {
//                auto archive = std::make_unique<YamlMapDeserializer>();
//                return archive->save(node);
//            }
//                //            case YAML::NodeType::Sequence:
//                //                return std::make_unique<YamlSequenceDeserializer>(node);
//                //            case YAML::NodeType::Scalar:
//                //                return std::make_unique<YamlScalarDeserializer>(node);
//            case YAML::NodeType::Null: {
//                auto archive = std::make_unique<YamlNullDeserializer>(node);
//                return archive->save(node);
//            }
//            case YAML::NodeType::Undefined:
//                throw std::runtime_error("");
//        }
//    }
//
//    std::unique_ptr<Archive> YamlMapDeserializer::tryGetKey(std::string_view sv) const {
//        for(auto it : _node) {
//            auto key = it.first.as<std::string>();
//            if(true) {
//                return decode(it.second);
//            }
//        }
//    }
//
//    data::StructElement YamlNullDeserializer::save(YAML::Node &node) {
//        if(!node.IsNull()) {
//            throw std::runtime_error("Expecting a null");
//        }
//        return node.as<std::string>();
//    }
//
//    data::StructElement YamlNullDeserializer::decode(YAML::Node &node) const {
//    }

} // namespace conv
