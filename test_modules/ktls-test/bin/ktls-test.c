#include "../include/ktls-test.h"

// Create MQTT CONNECT packet
int create_mqtt_connect_packet(char* buffer, const char* client_id) {
    int pos = 0;
    
    // Fixed header
    buffer[pos++] = 0x10;  // CONNECT packet type
    
    // Calculate remaining length
    int client_id_len = strlen(client_id);
    int remaining_length = 10 + 2 + client_id_len;  // Protocol name + flags + client ID
    buffer[pos++] = remaining_length;
    
    // Protocol name "MQTT"
    buffer[pos++] = 0x00; buffer[pos++] = 0x04;
    buffer[pos++] = 'M'; buffer[pos++] = 'Q'; 
    buffer[pos++] = 'T'; buffer[pos++] = 'T';
    
    // Protocol level (4 for MQTT 3.1.1)
    buffer[pos++] = 0x04;
    
    // Connect flags (clean session)
    buffer[pos++] = 0x02;
    
    // Keep alive (60 seconds)
    buffer[pos++] = 0x00; buffer[pos++] = 0x3C;
    
    // Client ID length
    buffer[pos++] = (client_id_len >> 8) & 0xFF;
    buffer[pos++] = client_id_len & 0xFF;
    
    // Client ID
    memcpy(buffer + pos, client_id, client_id_len);
    pos += client_id_len;
    
    return pos;
}

// Create MQTT PUBLISH packet
int create_mqtt_publish_packet(char* buffer, const char* topic, const char* message, uint16_t packet_id) {
    int pos = 0;
    int topic_len = strlen(topic);
    int message_len = strlen(message);
    
    // Fixed header - PUBLISH with QoS 1
    buffer[pos++] = 0x32;  // PUBLISH packet type with QoS 1
    
    // Calculate remaining length
    int remaining_length = 2 + topic_len + 2 + message_len;  // topic_length + topic + packet_id + message
    buffer[pos++] = remaining_length;
    
    // Topic length
    buffer[pos++] = (topic_len >> 8) & 0xFF;
    buffer[pos++] = topic_len & 0xFF;
    
    // Topic
    memcpy(buffer + pos, topic, topic_len);
    pos += topic_len;
    
    // Packet identifier (for QoS 1)
    buffer[pos++] = (packet_id >> 8) & 0xFF;
    buffer[pos++] = packet_id & 0xFF;
    
    // Message payload
    memcpy(buffer + pos, message, message_len);
    pos += message_len;
    
    return pos;
}

// Create MQTT SUBSCRIBE packet
int create_mqtt_subscribe_packet(char* buffer, const char* topic, uint16_t packet_id) {
    int pos = 0;
    int topic_len = strlen(topic);
    
    // Fixed header - SUBSCRIBE with QoS 1
    buffer[pos++] = 0x82;  // SUBSCRIBE packet type
    
    // Calculate remaining length
    int remaining_length = 2 + 2 + topic_len + 1;  // packet_id + topic_length + topic + qos
    buffer[pos++] = remaining_length;
    
    // Packet identifier
    buffer[pos++] = (packet_id >> 8) & 0xFF;
    buffer[pos++] = packet_id & 0xFF;
    
    // Topic length
    buffer[pos++] = (topic_len >> 8) & 0xFF;
    buffer[pos++] = topic_len & 0xFF;
    
    // Topic
    memcpy(buffer + pos, topic, topic_len);
    pos += topic_len;
    
    // QoS level
    buffer[pos++] = 0x01;  // QoS 1
    
    return pos;
}

int create_socket(const char* hostname, int port) {
    struct hostent* host_entry;
    struct sockaddr_in addr;
    int sockfd = -1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -1;
    }

    host_entry = gethostbyname(hostname);
    if (!host_entry) {
        fprintf(stderr, "Failed to resolve hostname: %s\n", hostname);
        close(sockfd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connection failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int check_ktls(SSL* ssl, int sockfd) {
    printf("=== Check ktls TX and Rx status ===\n");
    
    BIO *wbio = SSL_get_wbio(ssl);
    BIO *rbio = SSL_get_rbio(ssl);
    
    printf("kTLS Status:\n");
    printf("  TX kTLS: %s\n", BIO_get_ktls_send(wbio) ? "ACTIVE" : "INACTIVE");
    printf("  RX kTLS: %s\n", BIO_get_ktls_recv(rbio) ? "ACTIVE" : "INACTIVE");
    
    if (BIO_get_ktls_send(wbio) && BIO_get_ktls_recv(rbio)) {
        printf("✅ Both TX and RX kTLS are active\n");
        return 0;
    } else {
        printf("❌ RX kTLS still not active\n");
        return -1;
    }
}

// Parse and display received MQTT message
void parse_mqtt_message(char* buffer, int length) {
    if (length < 2) {
        printf("❌ Message too short\n");
        return;
    }
    
    uint8_t msg_type = (buffer[0] >> 4) & 0x0F;
    uint8_t flags = buffer[0] & 0x0F;
    uint8_t remaining_length = buffer[1];
    
    printf("📨 MQTT Message Received:\n");
    printf("   Type: ");
    
    switch (msg_type) {
        case 1: printf("CONNECT"); break;
        case 2: printf("CONNACK"); break;
        case 3: printf("PUBLISH"); break;
        case 4: printf("PUBACK"); break;
        case 5: printf("PUBREC"); break;
        case 6: printf("PUBREL"); break;
        case 7: printf("PUBCOMP"); break;
        case 8: printf("SUBSCRIBE"); break;
        case 9: printf("SUBACK"); break;
        case 10: printf("UNSUBSCRIBE"); break;
        case 11: printf("UNSUBACK"); break;
        case 12: printf("PINGREQ"); break;
        case 13: printf("PINGRESP"); break;
        case 14: printf("DISCONNECT"); break;
        default: printf("UNKNOWN(%d)", msg_type); break;
    }
    printf(" (Length: %d bytes)\n", remaining_length);
    
    // Parse PUBLISH message
    if (msg_type == 3) {  // PUBLISH
        int pos = 2;  // Skip fixed header
        
        // Topic length
        if (pos + 2 > length) return;
        uint16_t topic_len = (buffer[pos] << 8) | buffer[pos + 1];
        pos += 2;
        
        // Topic
        if (pos + topic_len > length) return;
        char topic[256];
        memcpy(topic, buffer + pos, topic_len);
        topic[topic_len] = '\0';
        pos += topic_len;
        
        // Packet ID (if QoS > 0)
        uint16_t packet_id = 0;
        if (flags & 0x06) {  // QoS 1 or 2
            if (pos + 2 > length) return;
            packet_id = (buffer[pos] << 8) | buffer[pos + 1];
            pos += 2;
        }
        
        // Message payload
        int payload_len = length - pos;
        if (payload_len > 0) {
            char payload[1024];
            memcpy(payload, buffer + pos, payload_len);
            payload[payload_len] = '\0';
            
            printf("   📍 Topic: %s\n", topic);
            if (packet_id > 0) {
                printf("   🆔 Packet ID: %d\n", packet_id);
            }
            printf("   📄 Payload: %s\n", payload);
        }
    }
    
    // Show raw hex for debugging
    printf("   🔍 Raw hex: ");
    for (int i = 0; i < length && i < 50; i++) {
        printf("%02x ", (unsigned char)buffer[i]);
    }
    if (length > 50) printf("...");
    printf("\n\n");
}