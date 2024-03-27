#include <logging.hpp>
#include <plugin.hpp>

class GenComponentLoader : public ggapi::Plugin {
private:
    
    ggapi::ModuleScope registerGenComponent(ggapi::Symbol, const ggapi::Container &callData);

    ggapi::Subscription _fetchTesFromCloudSubs;

public:
    bool onInitialize(ggapi::Struct data) override;

    bool onStart(ggapi::Struct data) override;

    static GenComponentLoader &get() {
        static GenComponentLoader instance{};
        return instance;
    }
};
