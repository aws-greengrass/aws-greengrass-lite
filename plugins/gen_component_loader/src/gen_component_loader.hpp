#include <logging.hpp>
#include <plugin.hpp>

class GenComponentLoader : public ggapi::Plugin {
private:
    
    ggapi::ObjHandle registerGenComponent(ggapi::Symbol, const ggapi::Container &callData);
    ggapi::Subscription _delegateComponentSubscription;

public:
    bool onInitialize(ggapi::Struct data) override;

    static GenComponentLoader &get() {
        static GenComponentLoader instance{};
        return instance;
    }
};
