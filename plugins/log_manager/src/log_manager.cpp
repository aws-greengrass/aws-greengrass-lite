#include "log_manager.hpp"

bool LogManager::onInitialize(ggapi::Struct data) {
    // TODO: retrieve and process system config
    return true;
}

bool LogManager::onStart(ggapi::Struct data) {
    // TODO: subscribe to LPC topics and register log uploading callback
    return true;
}

bool LogManager::onStop(ggapi::Struct data) {
    return true;
}

bool LogManager::onError_stop(ggapi::Struct data) {
    return true;
}
