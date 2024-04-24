#pragma once
#include <futures.hpp>
#include <scopes.hpp>
#include <utility>

namespace ipc_server {
    /**
     * A Promise explicitly associated with a module scope
     */
    struct BoundPromise {
        ggapi::ModuleScope module;
        ggapi::Promise promise;

        BoundPromise(ggapi::ModuleScope m, ggapi::Promise p)
            : module(std::move(m)), promise(std::move(p)) {
        }
    };
} // namespace ipc_server
