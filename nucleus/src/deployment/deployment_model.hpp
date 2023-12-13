#pragma once
#include "data/string_table.hpp"
#include <cstdint>
#include <util.hpp>

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
        KERNEL_ROLLBACK,

        /**
         * Deployment executes component bootstrap steps for the rollback, after BOOTSTRAP or
         * KERNEL_ACTIVATION fails. Only used when a specific config flag has been set for one or
         * more components in the rollback set.
         */
        ROLLBACK_BOOTSTRAP
    };
    struct DeploymentConsts {
        inline static const data::SymbolInit DEFAULT_SYM{"DEFAULT"};
        inline static const data::SymbolInit BOOTSTRAP_SYM{"BOOTSTRAP"};
        inline static const data::SymbolInit KERNEL_ACTIVATION_SYM{"KERNEL_ACTIVATION"};
        inline static const data::SymbolInit KERNEL_ROLLBACK_SYM{"KERNEL_ROLLBACK"};
        inline static const data::SymbolInit ROLLBACK_BOOTSTRAP_SYM{"ROLLBACK_BOOTSTRAP"};
        inline static const util::LookupTable<data::Symbol, DeploymentStage, 5> STAGE_MAP{
            DEFAULT_SYM,
            DeploymentStage::DEFAULT,
            BOOTSTRAP_SYM,
            DeploymentStage::BOOTSTRAP,
            KERNEL_ACTIVATION_SYM,
            DeploymentStage::KERNEL_ACTIVATION,
            KERNEL_ROLLBACK_SYM,
            DeploymentStage::KERNEL_ROLLBACK,
            ROLLBACK_BOOTSTRAP_SYM,
            DeploymentStage::ROLLBACK_BOOTSTRAP,
        };
    };
} // namespace deployment
