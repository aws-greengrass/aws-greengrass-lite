#include "serializable.hpp"
#include "conv/json_conv.hpp"
#include "conv/yaml_conv.hpp"
#include "data/shared_struct.hpp"
#include "scope/context_full.hpp"

namespace data {

    void AbstractArchiver::visit(data::Archive &other) {
        if(canVisit() && other->canVisit()) {
            ValueType v;
            other.visit(v);
            visit(v);
        }

        auto keySet = other.keys();
        for(auto &k : keySet) {
            auto me = key(k);
            auto otherKey = other.key(k);
            me->visit(otherKey);
        }
    }

    /**
     * Recursive visitor
     */
    void AbstractDearchiver::visit(data::Archive &other) {
        if(isList() || other->isList()) {
            // List visitor case
            auto me = list();
            auto otherList = other->list();
            while(me->canVisit() && otherList->canVisit()) {
                ValueType v;
                me->visit(v); // retrieve value
                otherList->visit(v); // write value
                me->advance();
                otherList->advance();
            }
        } else if(canVisit() && other->canVisit()) {
            // Scalar visitor case
            ValueType v;
            visit(v); // retrieve value
            other.visit(v); // write value
        }

        // Subkeys
        auto keySet = keys();
        for(auto &k : keySet) {
            auto me = key(k);
            auto otherKey = other.key(k);
            me->visit(otherKey);
        }
    }

    std::shared_ptr<ArchiveAdapter> ArchiveAdapter::key(const data::Symbol &symbol) {
        throw std::runtime_error("Not a structure");
    }

    std::shared_ptr<ArchiveAdapter> ArchiveAdapter::list() {
        throw std::runtime_error("Not a list");
    }

    void AbstractDearchiver::visit(data::Symbol &symbol) {
        std::string str;
        visit(str);
        symbol = scope::context()->intern(str);
    }

    std::vector<Symbol> ArchiveAdapter::keys() const {
        return {};
    }
    void AbstractDearchiver::visit(ValueType &vt) {
        vt = read().get();
    }
    void AbstractDearchiver::visit(bool &b) {
        b = read().getBool();
    }
    void AbstractDearchiver::visit(int32_t &i) {
        i = static_cast<int32_t>(read().getInt());
    }
    void AbstractDearchiver::visit(uint32_t &i) {
        i = static_cast<uint32_t>(read().getInt());
    }
    void AbstractDearchiver::visit(int64_t &i) {
        i = static_cast<int64_t>(read().getInt());
    }
    void AbstractDearchiver::visit(uint64_t &i) {
        i = static_cast<uint64_t>(read().getInt());
    }
    void AbstractDearchiver::visit(float &f) {
        f = static_cast<float>(read().getDouble());
    }
    void AbstractDearchiver::visit(double &d) {
        d = read().getDouble();
    }
    void AbstractDearchiver::visit(std::string &str) {
        str = read().getString();
    }
    std::shared_ptr<ArchiveAdapter> AbstractDearchiver::key(const Symbol &symbol) {
        auto el = read();
        if(el.isStruct()) {
            auto refStruct = el.getStruct();
            return std::make_shared<ElementDearchiver>(
                refStruct->get(refStruct->foldKey(symbol, isIgnoreCase())));
        } else if(!el.isNull()) {
            return ArchiveAdapter::key(symbol);
        } else {
            return std::make_shared<NullArchiveEntry>();
        }
    }
    std::vector<Symbol> AbstractDearchiver::keys() const {
        auto el = read();
        if(el.isStruct()) {
            return el.getStruct()->getKeys();
        } else {
            return {};
        }
    }
    std::shared_ptr<ArchiveAdapter> AbstractDearchiver::list() {
        auto el = read();
        if(el.isList()) {
            return std::make_shared<ListDearchiver>(el.castObject<ListModelBase>());
        } else if(!el.isNull()) {
            return ArchiveAdapter::list();
        } else {
            return std::make_shared<NullArchiveEntry>();
        }
    }
    bool AbstractDearchiver::canVisit() const {
        return true;
    }
    bool AbstractDearchiver::hasValue() const {
        return !read().isNull();
    }
    bool AbstractDearchiver::isList() const noexcept {
        return read().isList();
    }

    void Archive::readFromStruct(
        const std::shared_ptr<ContainerModelBase> &data, Serializable &target) {

        if(!data) {
            throw std::runtime_error("Structure/container is empty");
        }

        Archive archive(std::make_shared<ElementDearchiver>(data));
        target.visit(archive);
    }

    void Archive::writeToStruct(
        const std::shared_ptr<StructModelBase> &data, Serializable &target) {

        if(!data) {
            throw std::runtime_error("Structure is empty");
        }

        Archive archive(std::make_shared<StructArchiver>(data));
        target.visit(archive);
    }

    void Archive::readFromFile(const std::filesystem::path &file, Serializable &target) {
        std::string ext = util::lower(file.extension().generic_string());
        if(ext == ".yaml" || ext == ".yml") {
            readFromYamlFile(file, target);
        } else if(ext == ".json") {
            readFromJsonFile(file, target);
        } else {
            throw std::runtime_error("Unsupported file type");
        }
    }

    void Archive::readFromYamlFile(const std::filesystem::path &file, Serializable &target) {
        // TODO: Currently converts to struct first then deserialize, this can be made more
        // efficient, but let's get consistent first by creating a Yaml Dearchiver
        auto intermediate = std::make_shared<data::SharedStruct>(scope::context());
        conv::YamlReader reader(scope::context(), intermediate);
        reader.read(file);
        readFromStruct(intermediate, target);
    }

    void Archive::readFromJsonFile(const std::filesystem::path &file, Serializable &target) {
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
        target.visit(archive);
    }

    void Archive::writeToFile(const std::filesystem::path &file, Serializable &target) {
        std::string ext = util::lower(file.extension().generic_string());
        if(ext == ".yaml" || ext == ".yml") {
            writeToYamlFile(file, target);
        } else if(ext == ".json") {
            writeToJsonFile(file, target);
        } else {
            throw std::runtime_error("Unsupported file type");
        }
    }

    void Archive::writeToJsonFile(const std::filesystem::path &, Serializable &) {
        throw std::runtime_error("Not yet implemented");
    }

    void Archive::writeToYamlFile(const std::filesystem::path &, Serializable &) {
        throw std::runtime_error("Not yet implemented");
    }

} // namespace data
