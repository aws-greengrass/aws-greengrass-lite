#include "ipc_plugin.hpp"

// Global initialization of the API
static const Aws::Crt::ApiHandle apiHandle{};
const Keys IpcPlugin::keys{};

/**
 *
 * @param phase
 * @param data
 */
void IpcPlugin::beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) {
    std::cout << "[provision-plugin] Running lifecycle provision plugin... "
              << ggapi::StringOrd{phase}.toString() << std::endl;
}

/**
 *
 * @param data
 * @return
 */
bool IpcPlugin::onBootstrap(ggapi::Struct data) {
    data.put(NAME, keys.serviceName);
    return true;
}

/**
 *
 * @param data
 * @return True if successful, false otherwise.
 */
bool IpcPlugin::onStart(ggapi::Struct data) {
    return true;
}

/**
 *
 * @param data
 * @return True if successful, false otherwise.
 */
bool IpcPlugin::onRun(ggapi::Struct data) {
    return true;
}
