# kTLS connectivity to IoTCore

This PoC demonstrates connecting to AWS IoT Core using MQTT over TLS with kernel TLS (kTLS).

## Setup Instructions

For testing the ktls-test, follow these steps:

### (1) Create Certificate Directory
Create a folder `cert` in the ktls-test directory and put your AWS IoT certificate files into it:

bash
mkdir cert

Place the following files in the `cert/` directory:
- `device-certificate.pem.crt` - Your device certificate
- `private-key.pem.key` - Your private key
- `root-ca.pem` - Amazon Root CA certificate

### (2) Configure IoT Endpoint
Edit `src/entry.c` and update the following variables:
- `iot_endpoint` - Your AWS IoT Core endpoint
- Cert file paths

### (3) Compile and Run
Compile the project:
bash
gcc -o ktls-test src/entry.c bin/ktls-test.c -lssl -lcrypto

Run the application:
bash
sudo ./ktls-test