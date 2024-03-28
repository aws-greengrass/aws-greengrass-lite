#include <logging.hpp>
#include <functional>
#include <plugin.hpp>

class GenComponentDelegate : public ggapi::Plugin, public util::RefObject<GenComponentDelegate> {
    struct ScriptSection : public ggapi::Serializable {
        std::optional<std::unordered_map<std::string, std::string>> envMap;
        std::string script;
        std::optional<bool> requiresPrivilege;
        std::optional<std::string> skipIf;
        std::optional<int64_t> timeout;

        void visit(ggapi::Archive &archive) override {
            archive.setIgnoreCase();
            archive("SetEnv", envMap);
            archive("Script", script);
            archive("RequiresPrivilege", requiresPrivilege);
            archive("SkipIf", skipIf);
            archive("Timeout", timeout);
        }
    };

    struct BootstrapSection : public ggapi::Serializable {
        std::optional<std::unordered_map<std::string, std::string>> envMap;
        std::optional<bool> bootstrapOnRollback;
        std::optional<std::string> script;
        std::optional<bool> requiresPrivilege;
        std::optional<int64_t> timeout;

        void visit(ggapi::Archive &archive) override {
            archive.setIgnoreCase();
            archive("SetEnv", envMap);
            archive("BootstrapOnRollback", bootstrapOnRollback);
            archive("Script", script);
            archive("RequiresPrivilege", requiresPrivilege);
            archive("Timeout", timeout);
        }
    };

    struct LifecycleSection : public ggapi::Serializable {
        std::optional<std::unordered_map<std::string, std::string>> envMap;
        std::optional<ScriptSection> install;
        std::optional<ScriptSection> run;
        std::optional<ScriptSection> startup;
        std::optional<ScriptSection> shutdown;
        std::optional<ScriptSection> recover;
        std::optional<BootstrapSection> bootstrap;
        std::optional<bool> bootstrapOnRollback;

        void helper(
            ggapi::Archive &archive, std::string_view name, std::optional<ScriptSection> &section) {

            // Complexity is to handle behavior when a string is used instead of struct

            if(archive.isArchiving()) {
                archive(name, section);
                return;
            }
            auto sec = archive[name];
            if(!sec) {
                return;
            }
            if(!sec.keys().empty()) {
                sec(section); // map/structure
            } else {
                // if not a map, expected to be a script
                section.emplace();
                sec(section.value().script);
            }
        }

        void helper(
            ggapi::Archive &archive,
            std::string_view name,
            std::optional<BootstrapSection> &section) {

            // Complexity is to handle behavior when a string is used instead of struct

            if(archive.isArchiving()) {
                archive(name, section);
                return;
            }
            auto sec = archive[name];
            if(!sec) {
                return;
            }
            if(!sec.keys().empty()) {
                sec(section); // map/structure
            }
            // if not a map, expected to be a script
            section.emplace();
            sec(section.value().script);
        }

        void visit(ggapi::Archive &archive) override {
            archive.setIgnoreCase();
            archive("SetEnv", envMap);
            helper(archive, "install", install);
            helper(archive, "run", run);
            helper(archive, "startup", startup);
            helper(archive, "shutdown", shutdown);
            helper(archive, "recover", recover);
            helper(archive, "bootstrap", bootstrap);
        }
    };

private:
    std::string _name;
    ggapi::Struct _recipe;
    using Environment = std::unordered_map<std::string, std::optional<std::string>>;
    Environment _globalEnv;
    LifecycleSection _lifecycle;

public:
    explicit GenComponentDelegate(const ggapi::Struct &data) {
        _recipe = data.get<ggapi::Struct>("recipe");
        _name = data.get<std::string>("componentName");
    }
    //  self-> To store a count to the class's object itself
    //            so that the Delegate remains in memory event after the GenComponentLoader returns
    //        self is passed as const as the reference count for the class itself should not be
    //        increased any further.
    static bool lifecycleCallback(
        const std::shared_ptr<GenComponentDelegate> &self,
        ggapi::ModuleScope,
        ggapi::Symbol event,
        ggapi::Struct data);

    ggapi::ModuleScope registerComponent();

    void processScript(ScriptSection section);

    bool onInitialize(ggapi::Struct data) override;
};

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
