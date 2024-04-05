#include "generic_serializer.hpp"

//TODO: Move within Serializable and provide a public api
namespace data {
    void ArchiveExtend::readFromFileStruct(
        const std::filesystem::path &file, const std::shared_ptr<SharedStruct> &target) {
        std::string ext = util::lower(file.extension().generic_string());
        if(ext == ".yaml" || ext == ".yml") {
            conv::YamlReader reader(scope::context(), target);
            reader.read(file);
        } else if(ext == ".json") {
           std::cout<<"TODO::Will Support in future"<<std::endl;
        } else {
            throw std::runtime_error("Unsupported file type");
        }
    }
} // namespace data
