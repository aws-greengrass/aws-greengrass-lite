#include "generic_serializer.hpp"

namespace data {
    void ArchiveExtend::readFromFileStruct(
        const std::filesystem::path &file, const std::shared_ptr<SharedStruct> &target) {
        std::string ext = util::lower(file.extension().generic_string());
        if(ext == ".yaml" || ext == ".yml") {
            conv::YamlReader reader(scope::context(), target);
            reader.read(file);
        } else if(ext == ".json") {
           int a =1;

        } else {
            throw std::runtime_error("Unsupported file type");
        }
    }
} // namespace data
