#ifndef KTLS_TEST_H
#define KTLS_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <linux/tls.h>
#include <time.h>
#include <errno.h>

#define IOT_CORE_PORT 8883
#define BUFFER_SIZE 1024

// Function declarations for MQTT packet creation
int create_mqtt_connect_packet(char* buffer, const char* client_id);
int create_mqtt_publish_packet(char* buffer, const char* topic, const char* message, uint16_t packet_id);
int create_mqtt_subscribe_packet(char* buffer, const char* topic, uint16_t packet_id);

// Function declarations for network and SSL operations
int create_socket(const char* hostname, int port);
int check_ktls(SSL* ssl, int sockfd);

// Function declarations for MQTT message parsing
void parse_mqtt_message(char* buffer, int length);

#endif