#include "server_bootstrap.hpp"

#include <shared_device_sdk.hpp>

namespace Aws::Crt::Io {
    ServerBootstrap::ServerBootstrap(EventLoopGroup &elGroup, Allocator *allocator) noexcept
        : m_bootstrap(aws_server_bootstrap_new(allocator, elGroup.GetUnderlyingHandle())),
          m_lastError(m_bootstrap ? 0 : aws_last_error()) {
    }

    /**
     * Uses the default EventLoopGroup and HostResolver.
     * See Aws::Crt::ApiHandle::GetOrCreateStaticDefaultEventLoopGroup
     * and Aws::Crt::ApiHandle::GetOrCreateStaticDefaultHostResolver
     */
    ServerBootstrap::ServerBootstrap(Allocator *allocator) noexcept
        : ServerBootstrap{*Crt::ApiHandle::GetOrCreateStaticDefaultEventLoopGroup(), allocator} {
    }

    ServerBootstrap::~ServerBootstrap() noexcept {
        if(m_bootstrap) {
            aws_server_bootstrap_release(m_bootstrap);
        }
    }
} // namespace Aws::Crt::Io
