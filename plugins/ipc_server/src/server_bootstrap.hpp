#pragma once
#include <shared_device_sdk.hpp>

struct aws_server_bootstrap;

namespace Aws::Crt::Io {
    class EventLoopGroup;

    /**
     * A ServerBootstrap handles creation and setup of socket listeners
     * for accepting incoming connections
     */
    class ServerBootstrap final {
    public:
        /**
         * @param elGroup: EventLoopGroup to use.
         * @param allocator memory allocator to use
         */
        explicit ServerBootstrap(
            EventLoopGroup &elGroup, Allocator *allocator = ApiAllocator()) noexcept;

        /**
         * Uses the default EventLoopGroup and HostResolver.
         * See Aws::Crt::ApiHandle::GetOrCreateStaticDefaultEventLoopGroup
         * and Aws::Crt::ApiHandle::GetOrCreateStaticDefaultHostResolver
         */
        explicit ServerBootstrap(Allocator *allocator = ApiAllocator()) noexcept;

        ~ServerBootstrap() noexcept;
        ServerBootstrap(const ServerBootstrap &) = delete;
        ServerBootstrap &operator=(const ServerBootstrap &) = delete;
        ServerBootstrap(ServerBootstrap &&) = delete;
        ServerBootstrap &operator=(ServerBootstrap &&) = delete;

        /// @private
        [[nodiscard]] aws_server_bootstrap *GetUnderlyingHandle() const noexcept {
            return m_bootstrap;
        }

        /**
         * @return the value of the last aws error encountered by operations on this instance.
         */
        [[nodiscard]] int LastError() const noexcept {
            return m_lastError;
        }

        /**
         * @return true if the instance is in a valid state, false otherwise.
         */
        explicit operator bool() const noexcept {
            return static_cast<bool>(m_bootstrap);
        }

    private:
        aws_server_bootstrap *m_bootstrap;
        int m_lastError;
    };
} // namespace Aws::Crt::Io
