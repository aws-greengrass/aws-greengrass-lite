#include "serializable.hpp"
#include "conv/json_conv.hpp"
#include "conv/yaml_conv.hpp"
#include "data/shared_struct.hpp"
#include "scope/context_full.hpp"

namespace data {

    ArchiveTraits::SymbolType ArchiveTraits::toSymbol(const ArchiveTraits::ReadType &rv) {
        return scope::context()->intern(toString(rv));
    }

    ArchiveTraits::ValueType ArchiveTraits::toValue(const ArchiveTraits::ReadType &rv) {
        return rv.get();
    }

    double ArchiveTraits::toDouble(const ArchiveTraits::ReadType &rv) {
        return rv.getDouble();
    }

    uint64_t ArchiveTraits::toInt64(const ArchiveTraits::ReadType &rv) {
        return rv.getInt();
    }

    std::string ArchiveTraits::toString(const ArchiveTraits::ReadType &rv) {
        return rv.getString();
    }

    bool ArchiveTraits::toBool(const ArchiveTraits::ReadType &rv) {
        return rv.getBool();
    }

    bool ArchiveTraits::hasValue(const ArchiveTraits::ReadType &rv) {
        return !rv.isNull();
    }

    bool ArchiveTraits::isList(const ArchiveTraits::ReadType &rv) {
        return rv.isList();
    }

    std::shared_ptr<ArchiveAdapter> ArchiveTraits::toKey(
        const ArchiveTraits::ReadType &rv, const ArchiveTraits::KeyType &symbol, bool ignoreCase) {
        if(rv.isStruct()) {
            auto refStruct = rv.getStruct();
            return std::make_shared<ElementDearchiver>(
                refStruct->get(refStruct->foldKey(symbol, ignoreCase)));
        } else if(!rv.isNull()) {
            throw std::runtime_error("Not a Struct container");
        } else {
            return util::NullArchiveEntry<ArchiveTraits>::getNull();
        }
    }

    std::vector<ArchiveTraits::KeyType> ArchiveTraits::toKeys(const ArchiveTraits::ReadType &rv) {
        if(rv.isStruct()) {
            return rv.getStruct()->getKeys();
        } else {
            return {};
        }
    }

    std::shared_ptr<ArchiveAdapter> ArchiveTraits::toList(const ArchiveTraits::ReadType &rv) {
        if(rv.isList()) {
            return std::make_shared<ListDearchiver>(rv.castObject<ListModelBase>());
        } else if(!rv.isNull()) {
            throw std::runtime_error("Not a List container");
        } else {
            return util::NullArchiveEntry<ArchiveTraits>::getNull();
        }
    }

    namespace archive {
        void readFromStruct(const std::shared_ptr<ContainerModelBase> &data, Serializable &target) {

            if(!data) {
                throw std::runtime_error("Structure/container is empty");
            }

            Archive archive(std::make_shared<ElementDearchiver>(data));
            archive.visit(target);
        }

        void writeToStruct(const std::shared_ptr<StructModelBase> &data, Serializable &target) {

            if(!data) {
                throw std::runtime_error("Structure is empty");
            }

            Archive archive(std::make_shared<StructArchiver>(data));
            archive.visit(target);
        }

        void readFromFile(const std::filesystem::path &file, Serializable &target) {
            std::string ext = util::lower(file.extension().generic_string());
            if(ext == ".yaml" || ext == ".yml") {
                readFromYamlFile(file, target);
            } else if(ext == ".json") {
                readFromJsonFile(file, target);
            } else {
                throw std::runtime_error("Unsupported file type");
            }
        }

        void readFromYamlFile(const std::filesystem::path &file, Serializable &target) {
            // TODO: Currently converts to struct first then deserialize, this can be made more
            // efficient, but let's get consistent first by creating a Yaml Dearchiver
            auto intermediate = std::make_shared<data::SharedStruct>(scope::context());
            conv::YamlReader reader(scope::context(), intermediate);
            reader.read(file);
            readFromStruct(intermediate, target);
        }

        void readFromJsonFile(const std::filesystem::path &file, Serializable &target) {
            // TODO: Currently converts to struct first then deserialize, this can be made more
            // efficient, but let's get consistent first
            std::ifstream stream;
            stream.open(file, std::ios_base::in);
            if(!stream) {
                throw std::runtime_error("Unable to read from " + file.generic_string());
            }
            auto intermediate = std::make_shared<data::SharedStruct>(scope::context());
            conv::JsonReader reader(scope::context());
            data::StructElement value;
            reader.push(std::make_unique<conv::JsonElementResponder>(reader, value));
            rapidjson::ParseResult result = reader.read(stream);
            stream.close();
            if(!result) {
                throw errors::JsonParseError();
            }
            Archive archive(std::make_shared<ElementDearchiver>(value));
            archive.visit(target);
        }

        void writeToFile(const std::filesystem::path &file, Serializable &target) {
            std::string ext = util::lower(file.extension().generic_string());
            if(ext == ".yaml" || ext == ".yml") {
                writeToYamlFile(file, target);
            } else if(ext == ".json") {
                writeToJsonFile(file, target);
            } else {
                throw std::runtime_error("Unsupported file type");
            }
        }

        void writeToJsonFile(const std::filesystem::path &, Serializable &) {
            throw std::runtime_error("Not yet implemented");
        }

        void writeToYamlFile(const std::filesystem::path &, Serializable &) {
            throw std::runtime_error("Not yet implemented");
        }

    } // namespace archive

} // namespace data
