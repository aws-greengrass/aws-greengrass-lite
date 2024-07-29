/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mqtt.h"
#include "args.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include "ggl/utils.h"
#include "tls.h"
#include <assert.h>
#include <core_mqtt.h>
#include <core_mqtt_config.h>
#include <core_mqtt_serializer.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <transport_interface.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdnoreturn.h>

#ifndef IOTCORED_KEEP_ALIVE_PERIOD
#define IOTCORED_KEEP_ALIVE_PERIOD 30
#endif

#ifndef IOTCORED_CONNACK_TIMEOUT
#define IOTCORED_CONNACK_TIMEOUT 10
#endif

#ifndef IOTCORED_NETWORK_BUFFER_SIZE
#define IOTCORED_NETWORK_BUFFER_SIZE 5000
#endif

static uint32_t time_ms(void);
static void event_callback(
    MQTTContext_t *ctx,
    MQTTPacketInfo_t *packet_info,
    MQTTDeserializedInfo_t *deserialized_info
);

struct NetworkContext {
    IotcoredTlsCtx *tls_ctx;
};

static pthread_t recv_thread;
static pthread_t keepalive_thread;

static atomic_bool ping_pending;

static NetworkContext_t net_ctx;

static MQTTContext_t mqtt_ctx;

static uint8_t network_buffer[IOTCORED_NETWORK_BUFFER_SIZE];

pthread_mutex_t *coremqtt_get_send_mtx(const MQTTContext_t *ctx) {
    (void) ctx;
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    return &mtx;
}

pthread_mutex_t *coremqtt_get_state_mtx(const MQTTContext_t *ctx) {
    (void) ctx;
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    return &mtx;
}

static uint32_t time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t) (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

noreturn static void *mqtt_recv_thread_fn(void *arg) {
    MQTTContext_t *ctx = arg;
    while (true) {
        MQTTStatus_t mqtt_ret = MQTT_ReceiveLoop(ctx);

        if ((mqtt_ret != MQTTSuccess) && (mqtt_ret != MQTTNeedMoreBytes)) {
            GGL_LOGE("mqtt", "Error in receive loop, closing connection.");
            pthread_cancel(keepalive_thread);
            iotcored_tls_cleanup(
                ctx->transportInterface.pNetworkContext->tls_ctx
            );
            pthread_exit(NULL);
        }
    }
}

noreturn static void *mqtt_keepalive_thread_fn(void *arg) {
    MQTTContext_t *ctx = arg;

    while (true) {
        GglError err = ggl_sleep(IOTCORED_KEEP_ALIVE_PERIOD);
        if (err != GGL_ERR_OK) {
            break;
        }

        if (atomic_load(&ping_pending)) {
            GGL_LOGE(
                "mqtt",
                "Server did not respond to ping within Keep Alive period."
            );
            break;
        }

        GGL_LOGD("mqtt", "Sending pingreq.");
        atomic_store(&ping_pending, true);
        MQTTStatus_t mqtt_ret = MQTT_Ping(ctx);

        if (mqtt_ret != MQTTSuccess) {
            GGL_LOGE("mqtt", "Sending pingreq failed.");
            break;
        }
    }

    pthread_cancel(recv_thread);
    iotcored_tls_cleanup(ctx->transportInterface.pNetworkContext->tls_ctx);
    pthread_exit(NULL);
}

static int32_t transport_recv(
    NetworkContext_t *network_context, void *buffer, size_t bytes_to_recv
) {
    size_t bytes = bytes_to_recv < INT32_MAX ? bytes_to_recv : INT32_MAX;

    GglBuffer buf = { .data = buffer, .len = bytes };

    GglError ret = iotcored_tls_read(network_context->tls_ctx, &buf);

    return (ret == GGL_ERR_OK) ? (int32_t) buf.len : -1;
}

static int32_t transport_send(
    NetworkContext_t *network_context, const void *buffer, size_t bytes_to_send
) {
    size_t bytes = bytes_to_send < INT32_MAX ? bytes_to_send : INT32_MAX;

    GglError ret = iotcored_tls_write(
        network_context->tls_ctx,
        (GglBuffer) { .data = (void *) buffer, .len = bytes }
    );

    return (ret == GGL_ERR_OK) ? (int32_t) bytes : -1;
}

GglError iotcored_mqtt_connect(const IotcoredArgs *args) {
    TransportInterface_t transport = {
        .pNetworkContext = &net_ctx,
        .recv = transport_recv,
        .send = transport_send,
    };

    MQTTStatus_t mqtt_ret = MQTT_Init(
        &mqtt_ctx,
        &transport,
        time_ms,
        event_callback,
        &(MQTTFixedBuffer_t) { .pBuffer = network_buffer,
                               .size = sizeof(network_buffer) }
    );
    assert(mqtt_ret == MQTTSuccess);

    GglError ret = iotcored_tls_connect(args, &net_ctx.tls_ctx);
    if (ret != 0) {
        return ret;
    }

    size_t id_len = strlen(args->id);
    if (id_len > UINT16_MAX) {
        GGL_LOGE("mqtt", "Client ID too long.");
        return GGL_ERR_CONFIG;
    }

    MQTTConnectInfo_t conn_info = {
        .pClientIdentifier = args->id,
        .clientIdentifierLength = (uint16_t) id_len,
        .keepAliveSeconds = IOTCORED_KEEP_ALIVE_PERIOD,
        .cleanSession = true,
    };

    bool server_session = false;
    mqtt_ret = MQTT_Connect(
        &mqtt_ctx,
        &conn_info,
        NULL,
        IOTCORED_CONNACK_TIMEOUT * 1000,
        &server_session
    );

    if (mqtt_ret != MQTTSuccess) {
        GGL_LOGE(
            "mqtt", "Connection failed: %s", MQTT_Status_strerror(mqtt_ret)
        );
        return GGL_ERR_FAILURE;
    }

    atomic_store(&ping_pending, false);
    pthread_create(&recv_thread, NULL, mqtt_recv_thread_fn, &mqtt_ctx);
    pthread_create(
        &keepalive_thread, NULL, mqtt_keepalive_thread_fn, &mqtt_ctx
    );

    GGL_LOGI("mqtt", "Successfully connected.");

    return 0;
}

GglError iotcored_mqtt_publish(const IotcoredMsg *msg, uint8_t qos) {
    assert(msg != NULL);

    MQTTStatus_t result = MQTT_Publish(
        &mqtt_ctx,
        &(MQTTPublishInfo_t) {
            .pTopicName = (char *) msg->topic.data,
            .topicNameLength = (uint16_t) msg->topic.len,
            .pPayload = msg->payload.data,
            .payloadLength = msg->payload.len,
            .qos = qos,
        },
        MQTT_GetPacketId(&mqtt_ctx)
    );

    if (result != MQTTSuccess) {
        GGL_LOGE(
            "mqtt",
            "%s to %.*s failed: %s",
            "Publish",
            (int) (uint16_t) msg->topic.len,
            msg->topic.data,
            MQTT_Status_strerror(result)
        );
        return GGL_ERR_FAILURE;
    }

    GGL_LOGD(
        "mqtt",
        "Publish sent on: %.*s",
        (int) (uint16_t) msg->topic.len,
        msg->topic.data
    );

    return 0;
}

GglError iotcored_mqtt_subscribe(GglBuffer topic_filter, uint8_t qos) {
    MQTTStatus_t result = MQTT_Subscribe(
        &mqtt_ctx,
        &(MQTTSubscribeInfo_t) {
            .pTopicFilter = (char *) topic_filter.data,
            .topicFilterLength = (uint16_t) topic_filter.len,
            .qos = qos,
        },
        1,
        MQTT_GetPacketId(&mqtt_ctx)
    );

    if (result != MQTTSuccess) {
        GGL_LOGE(
            "mqtt",
            "%s to %.*s failed: %s",
            "Subscribe",
            (int) (uint16_t) topic_filter.len,
            topic_filter.data,
            MQTT_Status_strerror(result)
        );
        return GGL_ERR_FAILURE;
    }

    GGL_LOGD(
        "mqtt",
        "Publish sent on: %.*s",
        (int) (uint16_t) topic_filter.len,
        topic_filter.data
    );

    return 0;
}

bool iotcored_mqtt_topic_filter_match(GglBuffer topic_filter, GglBuffer topic) {
    bool matches = false;
    MQTTStatus_t result = MQTT_MatchTopic(
        (char *) topic.data,
        (uint16_t) topic.len,
        (char *) topic_filter.data,
        (uint16_t) topic_filter.len,
        &matches
    );
    return (result == MQTTSuccess) && matches;
}

static void event_callback(
    MQTTContext_t *ctx,
    MQTTPacketInfo_t *packet_info,
    MQTTDeserializedInfo_t *deserialized_info
) {
    assert(ctx != NULL);
    assert(packet_info != NULL);
    assert(deserialized_info != NULL);

    (void) ctx;

    if ((packet_info->type & 0xF0U) == MQTT_PACKET_TYPE_PUBLISH) {
        assert(deserialized_info->pPublishInfo != NULL);
        MQTTPublishInfo_t *publish = deserialized_info->pPublishInfo;

        GGL_LOGD(
            "mqtt",
            "Received publish id %u on topic %.*s.",
            deserialized_info->packetIdentifier,
            (int) publish->topicNameLength,
            publish->pTopicName
        );

        IotcoredMsg msg = { .topic = { .data = (uint8_t *) publish->pTopicName,
                                       .len = publish->topicNameLength },
                            .payload = { .data = (uint8_t *) publish->pPayload,
                                         .len = publish->payloadLength } };

        iotcored_mqtt_receive(&msg);
    } else {
        /* Handle other packets. */
        switch (packet_info->type) {
        case MQTT_PACKET_TYPE_PUBACK:
            GGL_LOGD(
                "mqtt",
                "Received %s id %u.",
                "puback",
                deserialized_info->packetIdentifier
            );
            break;
        case MQTT_PACKET_TYPE_SUBACK:
            GGL_LOGD(
                "mqtt",
                "Received %s id %u.",
                "suback",
                deserialized_info->packetIdentifier
            );
            break;
        case MQTT_PACKET_TYPE_UNSUBACK:
            GGL_LOGD(
                "mqtt",
                "Received %s id %u.",
                "unsuback",
                deserialized_info->packetIdentifier
            );
            break;
        case MQTT_PACKET_TYPE_PINGRESP:
            GGL_LOGD("mqtt", "Received pingresp.");
            atomic_store(&ping_pending, false);
            break;
        default:
            GGL_LOGE(
                "mqtt", "Received unknown packet type %02x.", packet_info->type
            );
        }
    }
}
