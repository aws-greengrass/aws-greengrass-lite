#pragma once

#include <aws/common/byte_order.h>
#include <aws/common/logging.h>
#include <aws/common/uuid.h>
#include <aws/crt/Allocator.h>
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/UUID.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpProxyStrategy.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/SocketOptions.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/io/Uri.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/event-stream/event_stream.h>
#include <aws/event-stream/event_stream_rpc_server.h>
#include <aws/http/connection.h>
#include <aws/http/private/request_response_impl.h>
#include <aws/http/request_response.h>
#include <aws/http/server.h>
#include <aws/http/status_code.h>
#include <aws/io/channel_bootstrap.h>
#include <aws/io/event_loop.h>
#include <aws/io/logging.h>
#include <aws/io/socket.h>
#include <aws/io/stream.h>
#include <aws/iot/Mqtt5Client.h>

#ifdef EXPORT_DEVICESDK_API
#if defined(_WIN32)
// Apparently defining this ends up breaking desired behavior (cause not understood)
#define IMPEXP_DEVICE_SDK_API
#else
#define IMPEXP_DEVICE_SDK_API __attribute__((visibility("default")))
#endif
#else
// Nothing needed for import
#define IMPEXP_DEVICE_SDK_API
#endif

namespace util {
    IMPEXP_DEVICE_SDK_API Aws::Crt::ApiHandle &getDeviceSdkApiHandle();
    [[nodiscard]] IMPEXP_DEVICE_SDK_API std::string_view getAwsCrtErrorString(
        int errorCode) noexcept;
} // namespace util
