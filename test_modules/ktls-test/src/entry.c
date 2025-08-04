#include "../include/ktls-test.h"

// Fill up these paths and endpoint
const char* iot_endpoint = "";
const char* cert_file = "";
const char* key_file = "";
const char* ca_file = "";

int main() {
    SSL_CTX* ctx = NULL;
    SSL* ssl = NULL;
    int sockfd = -1;
    int ret = 0;

    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // Create SSL context
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        fprintf(stderr, "Failed to create SSL context\n");
        ERR_print_errors_fp(stderr);
        return -1;
    }

    // Force TLS 1.2 for better kTLS support
    SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);

    // Enable kTLS
    SSL_CTX_set_options(ctx, SSL_OP_ENABLE_KTLS);
    
    if (!(SSL_CTX_get_options(ctx) & SSL_OP_ENABLE_KTLS)) {
        fprintf(stderr, "Warning: kTLS not supported by this OpenSSL build\n");
    }

    // Load certificates
    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "Failed to load certificate file\n");
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "Failed to load private key file\n");
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "Private key does not match certificate\n");
        goto cleanup;
    }

    if (!SSL_CTX_load_verify_locations(ctx, ca_file, NULL)) {
        fprintf(stderr, "Failed to load CA certificate\n");
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    // Create socket and SSL connection
    sockfd = create_socket(iot_endpoint, IOT_CORE_PORT);
    if (sockfd < 0) {
        goto cleanup;
    }

    ssl = SSL_new(ctx);
    if (!ssl) {
        fprintf(stderr, "Failed to create SSL structure\n");
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }

    SSL_set_fd(ssl, sockfd);
    SSL_set_tlsext_host_name(ssl, iot_endpoint);

    // TLS handshake
    ret = SSL_connect(ssl);
    if (ret <= 0) {
        fprintf(stderr, "TLS handshake failed\n");
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }

    printf("TLS connection established successfully!\n");
    printf("TLS version: %s\n", SSL_get_version(ssl));
    printf("Cipher: %s\n", SSL_get_cipher(ssl));

    if (check_ktls(ssl, sockfd) != 0) {
        fprintf(stderr, "Manual kTLS setup failed\n");
        goto cleanup;
    }

    // MQTT CONNECT
    printf("\n📡 Sending MQTT CONNECT...\n");
    char mqtt_packet[256];
    int packet_len = create_mqtt_connect_packet(mqtt_packet, "ktls-mqtt-client");
    
    int bytes_written = write(sockfd, mqtt_packet, packet_len);
    if (bytes_written <= 0) {
        fprintf(stderr, "Failed to send CONNECT\n");
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }
    printf("✅ Sent MQTT CONNECT (%d bytes)\n", bytes_written);

    // Read CONNACK
    char connack[4];
    int bytes_read = read(sockfd, connack, sizeof(connack));
    if (bytes_read <= 0 || connack[0] != 0x20) {
        fprintf(stderr, "Failed to receive CONNACK\n");
        goto cleanup;
    }
    printf("✅ Received CONNACK - Connected to IoT Core!\n");

    // MQTT SUBSCRIBE to test topic
    const char* test_topic = "test/ktls/demo";
    printf("\n📥 Subscribing to topic: %s\n", test_topic);
    packet_len = create_mqtt_subscribe_packet(mqtt_packet, test_topic, 1);
    
    bytes_written = write(sockfd, mqtt_packet, packet_len);
    if (bytes_written > 0) {
        printf("✅ Sent SUBSCRIBE (%d bytes)\n", bytes_written);
        
        // Read SUBACK
        char suback[5];
        read(sockfd, suback, sizeof(suback));
        printf("✅ Received SUBACK - Subscription confirmed\n");
    }

    // MQTT PUBLISH messages
    printf("\n📤 Publishing messages to topic: %s\n", test_topic);
    printf("💡 Go to AWS IoT Console → Test → MQTT test client → Subscribe to '%s'\n\n", test_topic);

    for (int i = 1; i <= 5; i++) {
        // Create message with timestamp
        char message[200];
        time_t now = time(NULL);
        snprintf(message, sizeof(message), 
                "{\"message\": \"Hello from Felicity!\", \"count\": %d, \"timestamp\": %ld, \"client\": \"ktls-mqtt-client\"}", 
                i, now);

        // Create and send PUBLISH packet
        packet_len = create_mqtt_publish_packet(mqtt_packet, test_topic, message, i);
        bytes_written = write(sockfd, mqtt_packet, packet_len);
        
        if (bytes_written > 0) {
            printf("📤 Message %d sent (%d bytes): %s\n", i, bytes_written, message);
            
            // Read PUBACK (QoS 1 acknowledgment)
            char puback[4];
            read(sockfd, puback, sizeof(puback));
            printf("✅ Received PUBACK for message %d\n", i);
        } else {
            fprintf(stderr, "Failed to send message %d\n", i);
        }

        sleep(3);  // Wait 3 seconds between messages
    }

    printf("\n🎉 All messages sent successfully using kTLS!\n");
    printf("Check the AWS IoT Console to see your messages!\n");

    // Keep connection alive to receive any incoming messages
    printf("\n👂 Listening for incoming messages (press Ctrl+C to exit)...\n");
    while (1) {
        char incoming[1024];
        int received = read(sockfd, incoming, sizeof(incoming));
        if (received > 0) {
            printf("📨 Received message (%d bytes)\n", received);
            // Parse and display MQTT message here if needed
            parse_mqtt_message(incoming, received);
        
            // Send PUBACK if it's a QoS 1 PUBLISH message
            if (received >= 2 && ((incoming[0] >> 4) & 0x0F) == 3) {  // PUBLISH
                uint8_t flags = incoming[0] & 0x0F;
                if (flags & 0x02) {  // QoS 1
                    // Extract packet ID and send PUBACK
                    if (received >= 4) {
                        uint16_t topic_len = (incoming[2] << 8) | incoming[3];
                        if (received >= 6 + topic_len) {
                            uint16_t packet_id = (incoming[4 + topic_len] << 8) | incoming[5 + topic_len];
                            
                            // Create PUBACK
                            char puback[4];
                            puback[0] = 0x40;  // PUBACK
                            puback[1] = 0x02;  // Remaining length
                            puback[2] = (packet_id >> 8) & 0xFF;
                            puback[3] = packet_id & 0xFF;
                            
                            write(sockfd, puback, 4);
                            printf("✅ Sent PUBACK for packet ID %d\n", packet_id);
                        }
                    }
                }
            }
        } else if (received == 0) {
            printf("🔌 Connection closed by server\n");
            break;
        } else {
            printf("❌ Read error: %s\n", strerror(errno));
            break;
        }
        sleep(1);
    }

cleanup:
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (sockfd >= 0) {
        close(sockfd);
    }
    if (ctx) {
        SSL_CTX_free(ctx);
    }
    EVP_cleanup();

    return ret > 0 ? 0 : -1;
}
