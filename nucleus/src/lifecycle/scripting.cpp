#include "scripting.hpp"
#include "deployment/deployment_manager.hpp"

bool ScriptRunner::willRun() {
    return false;
}

bool ScriptRunner::start() {
    std::cout << "script starting" << std::endl;
    return true;
}

void ScriptRunner::kill() {
}
