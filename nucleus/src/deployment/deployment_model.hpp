#pragma once

#include <cstdint>

namespace deployment {

    enum class DeploymentType : uint32_t { IOT_JOBS, LOCAL, SHADOW };

    enum class DeploymentStage : uint32_t {
        /**
         * Deployment workflow is non-intrusive, i.e. not impacting kernel runtime
         */
        DEFAULT,

        /**
         * Deployment goes over component bootstrap steps, which can be intrusive to kernel.
         */
        BOOTSTRAP,

        /**
         * Deployment has finished bootstrap steps and is in the middle of applying all changes to
         * Kernel.
         */
        KERNEL_ACTIVATION,

        /**
         * Deployment tries to rollback to Kernel with previous configuration, after BOOTSTRAP or
         * KERNEL_ACTIVATION fails.
         */
        KERNEL_ROLLBACK
    };
} // namespace deployment
