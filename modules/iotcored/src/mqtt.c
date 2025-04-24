// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "mqtt.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/utils.h"
#include "iotcored.h"
#include "subscription_dispatch.h"
#include "tls.h"
#include <assert.h>
#include <core_mqtt.h>
#include <core_mqtt_config.h>
#include <core_mqtt_serializer.h>
#include <ggl/backoff.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
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

#ifndef IOTCORED_UNACKED_PACKET_BUFFER_SIZE
#define IOTCORED_UNACKED_PACKET_BUFFER_SIZE (IOTCORED_NETWORK_BUFFER_SIZE * 3)
#endif

#define IOTCORED_MQTT_MAX_PUBLISH_RECORDS 10

static uint32_t time_ms(void);
static void event_callback(
    MQTTContext_t *ctx,
    MQTTPacketInfo_t *packet_info,
    MQTTDeserializedInfo_t *deserialized_info
);

struct NetworkContext {
    IotcoredTlsCtx *tls_ctx;
};

typedef struct {
    uint16_t packet_id;
    uint8_t *serialized_packet;
    size_t serialized_packet_len;
} StoredPublish;

static pthread_t recv_thread;
static pthread_t keepalive_thread;

static atomic_bool ping_pending;

static NetworkContext_t net_ctx;

static MQTTContext_t mqtt_ctx;

static const IotcoredArgs *iot_cored_args;

static uint8_t network_buffer[IOTCORED_NETWORK_BUFFER_SIZE];

static MQTTPubAckInfo_t
    outgoing_publish_records[IOTCORED_MQTT_MAX_PUBLISH_RECORDS];
// TODO: Remove once no longer needed by coreMQTT
static MQTTPubAckInfo_t incoming_publish_record;

static StoredPublish unacked_publishes[IOTCORED_MQTT_MAX_PUBLISH_RECORDS]
    = { 0 };

static uint8_t packet_store_buffer[IOTCORED_UNACKED_PACKET_BUFFER_SIZE];

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
    return (uint32_t) ((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

// This implementation assumes that we always compact the memory when a free()
// call is made.
static uint8_t *mqtt_pub_alloc(size_t length) {
    size_t i = 0;
    for (; i < IOTCORED_MQTT_MAX_PUBLISH_RECORDS; i++) {
        if (unacked_publishes[i].packet_id == 0) {
            break;
        }
    }

    if (i == IOTCORED_MQTT_MAX_PUBLISH_RECORDS) {
        GGL_LOGE("Not enough spots in record array to store one more packet.");
        return NULL;
    }

    uintptr_t last_packet_end;

    if (i == 0) {
        last_packet_end = (uintptr_t) &packet_store_buffer[0];
    } else {
        last_packet_end
            = ((uintptr_t) unacked_publishes[i - 1].serialized_packet)
            + ((uintptr_t) unacked_publishes[i - 1].serialized_packet_len);
    }

    size_t bytes_filled
        = (size_t) (last_packet_end - ((uintptr_t) &packet_store_buffer[0]));
    size_t space_left
        = (sizeof(packet_store_buffer) / sizeof(packet_store_buffer[0]))
        - bytes_filled;

    if (space_left < length) {
        GGL_LOGE("Not enough space in buffer to store one more packet.");
        return NULL;
    }

    return &packet_store_buffer[bytes_filled];
}

static void mqtt_pub_free(const uint8_t *ptr) {
    size_t i = 0;
    for (; i < IOTCORED_MQTT_MAX_PUBLISH_RECORDS; i++) {
        if ((unacked_publishes[i].packet_id != 0)
            && (unacked_publishes[i].serialized_packet == ptr)) {
            break;
        }
    }

    // If we cannot find the entry. Log the error and exit.
    if (i == IOTCORED_MQTT_MAX_PUBLISH_RECORDS) {
        GGL_LOGE("Cannot find a matching publish record entry to free.");
        return;
    }

    size_t byte_offset = unacked_publishes[i].serialized_packet_len;

    if (i != (IOTCORED_MQTT_MAX_PUBLISH_RECORDS - 1)) {
        size_t bytes_to_move
            = (size_t) (((uintptr_t) &packet_store_buffer
                             [IOTCORED_UNACKED_PACKET_BUFFER_SIZE - 1])
                        - (((uintptr_t) unacked_publishes[i].serialized_packet)
                           + unacked_publishes[i].serialized_packet_len)
                        + 1U);

        // Move the whole array after the freed packet forward in memory.
        memmove(
            unacked_publishes[i].serialized_packet,
            (unacked_publishes[i].serialized_packet
             + unacked_publishes[i].serialized_packet_len),
            bytes_to_move
        );

        // Compact the records.
        for (; i < IOTCORED_MQTT_MAX_PUBLISH_RECORDS - 1; i++) {
            if (unacked_publishes[i + 1].packet_id == 0) {
                break;
            }

            unacked_publishes[i].packet_id = unacked_publishes[i + 1].packet_id;
            unacked_publishes[i].serialized_packet
                = unacked_publishes[i + 1].serialized_packet - byte_offset;
            unacked_publishes[i].serialized_packet_len
                = unacked_publishes[i + 1].serialized_packet_len;
        }
    }

    // Clear the last record.
    unacked_publishes[i].packet_id = 0;
    unacked_publishes[i].serialized_packet = NULL;
    unacked_publishes[i].serialized_packet_len = 0;

    memset(
        &packet_store_buffer
            [(sizeof(packet_store_buffer) / sizeof(packet_store_buffer[0]))
             - byte_offset],
        0,
        byte_offset
    );
}

static bool mqtt_store_packet(
    MQTTContext_t *context, uint16_t packet_id, MQTTVec_t *mqtt_vec
) {
    (void) context;
    size_t i;
    for (i = 0; i < IOTCORED_MQTT_MAX_PUBLISH_RECORDS; i++) {
        if (unacked_publishes[i].packet_id == 0) {
            break;
        }
    }

    if (i == IOTCORED_MQTT_MAX_PUBLISH_RECORDS) {
        GGL_LOGE("No space left in array to store additional record.");
        return false;
    }

    size_t memory_needed = MQTT_GetBytesInMQTTVec(mqtt_vec);

    uint8_t *allocated_mem = mqtt_pub_alloc(memory_needed);
    if (allocated_mem == NULL) {
        return false;
    }

    MQTT_SerializeMQTTVec(allocated_mem, mqtt_vec);

    unacked_publishes[i].packet_id = packet_id;
    unacked_publishes[i].serialized_packet = allocated_mem;
    unacked_publishes[i].serialized_packet_len = memory_needed;

    GGL_LOGD("Stored MQTT publish (ID: %d).", packet_id);
    return true;
}

static bool mqtt_retrieve_packet(
    MQTTContext_t *context,
    uint16_t packet_id,
    uint8_t **serialized_mqtt_vec,
    size_t *serialized_mqtt_vec_len
) {
    (void) context;

    for (size_t i = 0; i < IOTCORED_MQTT_MAX_PUBLISH_RECORDS; i++) {
        if (unacked_publishes[i].packet_id == packet_id) {
            *serialized_mqtt_vec = unacked_publishes[i].serialized_packet;
            *serialized_mqtt_vec_len
                = unacked_publishes[i].serialized_packet_len;

            GGL_LOGD("Retrived MQTT publish (ID: %d).", packet_id);
            return true;
        }
    }

    GGL_LOGE("No packet with ID %d present.", packet_id);

    return false;
}

static void mqtt_clear_packet(MQTTContext_t *context, uint16_t packet_id) {
    (void) context;

    for (size_t i = 0; i < IOTCORED_MQTT_MAX_PUBLISH_RECORDS; i++) {
        if (unacked_publishes[i].packet_id == packet_id) {
            mqtt_pub_free(unacked_publishes[i].serialized_packet);
            GGL_LOGD("Cleared MQTT publish (ID: %d).", packet_id);
            return;
        }
    }

    GGL_LOGE("Cannot find the packet ID to clear.");
}

// Establish TLS and MQTT connection to the AWS IoT broker.
static GglError establish_connection(void *ctx) {
    (void) ctx;
    MQTTStatus_t mqtt_ret;

    GGL_LOGD("Trying to establish connection to IoT core.");

    GglError ret = iotcored_tls_connect(iot_cored_args, &net_ctx.tls_ctx);
    if (ret != 0) {
        GGL_LOGE("Failed to create TLS connection.");
        return ret;
    }

    size_t id_len = strlen(iot_cored_args->id);
    if (id_len > UINT16_MAX) {
        GGL_LOGE("Client ID too long.");
        return GGL_ERR_CONFIG;
    }

    MQTTConnectInfo_t conn_info = {
        .pClientIdentifier = iot_cored_args->id,
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
        GGL_LOGE("Connection failed: %s", MQTT_Status_strerror(mqtt_ret));
        return GGL_ERR_FAILURE;
    }

    atomic_store_explicit(&ping_pending, false, memory_order_release);

    GGL_LOGD("Connected to IoT core.");
    return GGL_ERR_OK;
}

noreturn static void *mqtt_recv_thread_fn(void *arg) {
    while (true) {
        // Connect to IoT core with backoff between 10ms->10s.
        ggl_backoff_indefinite(10, 10000, establish_connection, NULL);

        // Send status update to indicate mqtt (re)connection.
        iotcored_mqtt_status_update_send(ggl_obj_bool(true));

        iotcored_re_register_all_subs();

        MQTTStatus_t mqtt_ret;
        MQTTContext_t *ctx = arg;
        do {
            mqtt_ret = MQTT_ReceiveLoop(ctx);
        } while ((mqtt_ret == MQTTSuccess) || (mqtt_ret == MQTTNeedMoreBytes));

        GGL_LOGE("Error in receive loop, closing connection.");

        (void) MQTT_Disconnect(ctx);
        iotcored_tls_cleanup(ctx->transportInterface.pNetworkContext->tls_ctx);

        // Send status update to indicate mqtt disconnection.
        iotcored_mqtt_status_update_send(ggl_obj_bool(false));
    }
}

noreturn static void *mqtt_keepalive_thread_fn(void *arg) {
    MQTTContext_t *ctx = arg;

    while (true) {
        GglError err;
        do {
            err = ggl_sleep(IOTCORED_KEEP_ALIVE_PERIOD);
        } while (MQTT_CheckConnectStatus(ctx) == MQTTStatusNotConnected);

        if (err != GGL_ERR_OK) {
            GGL_LOGE("Failed a call to ggl_sleep.");
            break;
        }

        if (atomic_load_explicit(&ping_pending, memory_order_acquire)) {
            GGL_LOGE("Server did not respond to ping within Keep Alive period."
            );
            // We do not care about the value returned by the following call.
            (void) MQTT_Disconnect(ctx);
        } else {
            GGL_LOGD("Sending pingreq.");
            atomic_store_explicit(&ping_pending, true, memory_order_release);
            MQTTStatus_t mqtt_ret = MQTT_Ping(ctx);

            if (mqtt_ret != MQTTSuccess) {
                GGL_LOGE("Sending pingreq failed.");

                // We do not care about the value returned by the following
                // call.
                (void) MQTT_Disconnect(ctx);
            }
        }
    }

    GGL_LOGE("Exiting the MQTT Keep Alive thread.");

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

    mqtt_ret = MQTT_InitStatefulQoS(
        &mqtt_ctx,
        outgoing_publish_records,
        sizeof(outgoing_publish_records) / sizeof(*outgoing_publish_records),
        &incoming_publish_record,
        1
    );
    assert(mqtt_ret == MQTTSuccess);

    mqtt_ret = MQTT_InitRetransmits(
        &mqtt_ctx, mqtt_store_packet, mqtt_retrieve_packet, mqtt_clear_packet
    );
    assert(mqtt_ret == MQTTSuccess);

    // Store a global variable copy.
    iot_cored_args = args;

    int thread_ret
        = pthread_create(&recv_thread, NULL, mqtt_recv_thread_fn, &mqtt_ctx);
    if (thread_ret != 0) {
        GGL_LOGE("Could not create the MQTT receive thread: %d.", thread_ret);
        return GGL_ERR_FATAL;
    }

    thread_ret = pthread_create(
        &keepalive_thread, NULL, mqtt_keepalive_thread_fn, &mqtt_ctx
    );
    if (thread_ret != 0) {
        GGL_LOGE(
            "Could not create the MQTT keep-alive thread: %d.", thread_ret
        );
        return GGL_ERR_FATAL;
    }

    GGL_LOGI("Successfully connected.");

    return GGL_ERR_OK;
}

bool iotcored_mqtt_connection_status(void) {
    bool connected = false;
    if (MQTT_CheckConnectStatus(&mqtt_ctx) == MQTTStatusConnected) {
        connected = true;
    }
    return connected;
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
            "%s to %.*s failed: %s",
            "Publish",
            (int) (uint16_t) msg->topic.len,
            msg->topic.data,
            MQTT_Status_strerror(result)
        );
        return GGL_ERR_FAILURE;
    }

    GGL_LOGD(
        "Publish sent on: %.*s",
        (int) (uint16_t) msg->topic.len,
        msg->topic.data
    );

    return 0;
}

GglError iotcored_mqtt_subscribe(
    GglBuffer *topic_filters, size_t count, uint8_t qos
) {
    assert(count > 0);
    assert(count < GGL_MQTT_MAX_SUBSCRIBE_FILTERS);

    static MQTTSubscribeInfo_t sub_infos[GGL_MQTT_MAX_SUBSCRIBE_FILTERS];

    for (size_t i = 0; i < count; i++) {
        sub_infos[i] = (MQTTSubscribeInfo_t) {
            .pTopicFilter = (char *) topic_filters[i].data,
            .topicFilterLength = (uint16_t) topic_filters[i].len,
            .qos = qos,
        };
    }

    MQTTStatus_t result = MQTT_Subscribe(
        &mqtt_ctx, sub_infos, count, MQTT_GetPacketId(&mqtt_ctx)
    );

    if (result != MQTTSuccess) {
        GGL_LOGE(
            "%s to %.*s failed: %s",
            "Subscribe",
            (int) (uint16_t) topic_filters[0].len,
            topic_filters[0].data,
            MQTT_Status_strerror(result)
        );
        return GGL_ERR_FAILURE;
    }

    GGL_LOGD(
        "Subscribe sent for: %.*s",
        (int) (uint16_t) topic_filters[0].len,
        topic_filters[0].data
    );

    return 0;
}

GglError iotcored_mqtt_unsubscribe(GglBuffer *topic_filters, size_t count) {
    assert(count > 0);
    assert(count < GGL_MQTT_MAX_SUBSCRIBE_FILTERS);

    static MQTTSubscribeInfo_t sub_infos[GGL_MQTT_MAX_SUBSCRIBE_FILTERS];

    for (size_t i = 0; i < count; i++) {
        sub_infos[i] = (MQTTSubscribeInfo_t) {
            .pTopicFilter = (char *) topic_filters[i].data,
            .topicFilterLength = (uint16_t) topic_filters[i].len,
            .qos = 0,
        };
    }

    MQTTStatus_t result = MQTT_Unsubscribe(
        &mqtt_ctx, sub_infos, count, MQTT_GetPacketId(&mqtt_ctx)
    );

    if (result != MQTTSuccess) {
        GGL_LOGE(
            "%s to %.*s failed: %s",
            "Unsubscribe",
            (int) (uint16_t) topic_filters[0].len,
            topic_filters[0].data,
            MQTT_Status_strerror(result)
        );
        return GGL_ERR_FAILURE;
    }

    GGL_LOGD(
        "Unsubscribe sent for: %.*s",
        (int) (uint16_t) topic_filters[0].len,
        topic_filters[0].data
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
        // Handle other packets.
        switch (packet_info->type) {
        case MQTT_PACKET_TYPE_PUBACK:
            GGL_LOGD(
                "Received %s id %u.",
                "puback",
                deserialized_info->packetIdentifier
            );
            break;
        case MQTT_PACKET_TYPE_SUBACK:
            GGL_LOGD(
                "Received %s id %u.",
                "suback",
                deserialized_info->packetIdentifier
            );
            break;
        case MQTT_PACKET_TYPE_UNSUBACK:
            GGL_LOGD(
                "Received %s id %u.",
                "unsuback",
                deserialized_info->packetIdentifier
            );
            break;
        case MQTT_PACKET_TYPE_PINGRESP:
            GGL_LOGD("Received pingresp.");
            atomic_store_explicit(&ping_pending, false, memory_order_release);
            break;
        default:
            GGL_LOGE("Received unknown packet type %02x.", packet_info->type);
        }
    }
}
