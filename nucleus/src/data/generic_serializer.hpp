
#include "conv/json_conv.hpp"
#include "conv/yaml_conv.hpp"
#include "data/shared_struct.hpp"
#include "scope/context_full.hpp"
#include "serializable.hpp"

namespace data {

    class ArchiveExtend : public Archive {
        public:
        static void readFromFileStruct(
            const std::filesystem::path &file, const std::shared_ptr<SharedStruct> &target);
    };
} // namespace data
