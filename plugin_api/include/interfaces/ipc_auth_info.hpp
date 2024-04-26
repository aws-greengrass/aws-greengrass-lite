#pragma once
#include <api_archive.hpp>
#include <api_standard_errors.hpp>

namespace interfaces::ipc_auth_info {

    /**
     * RequestIpcInfo LPC call takes in a service name, and provides an authorization token and
     * socket endpoint for that service.
     */

    // TODO: Rename all symbols to better names, currently kept untouched to not break
    // existing code
    inline static const ggapi::Symbol interfaceTopic{"aws.greengrass.RequestIpcInfo"};

    /**
     * Request input structure, pass in service name - that is, the component, or a CLI
     */
    struct IpcAuthInfoIn : public ggapi::Serializable {

        std::string serviceName;

        void visit(util::ArchiveBase<ggapi::ArchiveTraits> &archive) override {
            archive(_serviceName, serviceName);
        }
        void validate() override {
            if(serviceName.empty()) {
                throw ggapi::ValidationError("Service name was not specified");
            }
        }

    private:
        ggapi::Symbol _serviceName{"serviceName"};
    };

    /**
     * Returned IPC information - the path to the socket and an auth token
     */
    struct IpcAuthInfoOut : public ggapi::Serializable {

        std::string socketPath;
        std::string authToken;

        void visit(util::ArchiveBase<ggapi::ArchiveTraits> &archive) override {
            archive(_socketPath, socketPath);
            archive(_authToken, authToken);
        }

    private:
        inline static const ggapi::Symbol _socketPath{"domain_socket_path"};
        inline static const ggapi::Symbol _authToken{"cli_auth_token"};
    };

} // namespace interfaces::ipc_auth_info
