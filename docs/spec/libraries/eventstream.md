## Event Stream

Event stream encoding provides bidirectional communication between a client and
a server. Data frames used by Greengrass IPC are encoded in this format.

Each message consists of two sections: the prelude and the data.

The prelude consists of:

- The total byte length of the message
- The combined byte length of all headers

The data section consists of:

- Headers
- Payload

Each section ends with a 4-byte big-endian integer cyclic redundancy check (CRC)
checksum. The message CRC checksum is for both the prelude section and the data
section. Event stream uses CRC32 (often referred to as GZIP CRC32) to calculate
both CRCs. For more information about CRC32, see GZIP file format specification
version 4.3.

Total message overhead, including the prelude and both checksums, is 16 bytes.

Each message contains the following components, in the following order:

- **Prelude**: Consists of two, 4-byte fields, for a fixed total of 8 bytes.
  - First 4 bytes: The big-endian integer byte-length of the entire message,
    inclusive of this 4-byte length field.
  - Second 4 bytes: The big-endian integer byte-length of the 'headers' portion
    of the message, excluding the 'headers' length field itself.
- **Prelude CRC**: The 4-byte CRC checksum for the prelude portion of the
  message, excluding the CRC itself. The prelude has a separate CRC from the
  message CRC. That ensures that event stream can detect corrupted byte-length
  information immediately without causing errors, such as buffer overruns.
- **Headers**: Metadata annotating the message; for example, message type and
  content type. Messages have multiple headers, which are key:value pairs, where
  the key is a UTF-8 string. Headers can appear in any order in the 'headers'
  portion of the message, and each header can appear only once.
- **Payload**: The payload data.
- **Message CRC**: The 4-byte CRC checksum from the start of the message to the
  start of the checksum. That is, everything in the message except the CRC
  itself.

Each header contains the following components; there are multiple headers per
frame.

- **Header name byte-length**: The 1-byte unsigned byte-length of the header
  name.
- **Header name**: The name of the header in UTF-8.
- **Header value type**: A 1-byte unsigned number indicating the header value.
  The following list shows the possible values for the header and what they
  indicate.
  - 0: True
  - 1: False
  - 2: Byte
  - 3: Int16
  - 4: Int32
  - 5: Int64
  - 6: Byte Buffer
  - 7: String
  - 8: Timestamp
  - 9: UUID
- **Header value**: The header value, according to the header value type. See
  the below for each value type.
  - **True**
    - No value field.
  - **False**
    - No value field.
  - **Byte**
    - 1 byte: signed 1-byte integer value.
  - **Int16**
    - 2 bytes: signed 2-byte big-endian integer value.
  - **Int32**
    - 4 bytes: signed 4-byte big-endian integer value.
  - **Int64**
    - 8 bytes: signed 8-byte big-endian integer value.
  - **Byte Buffer**
    - 2 bytes: The byte length of the byte buffer value
    - The value of the byte buffer.
  - **String**
    - 2 bytes: The byte length of the string value
    - The UTF-8 encoded value of the string.
  - **Timestamp**
    - 8 bytes: signed 8-byte big-endian integer containing milliseconds from
      epoch.
  - **UUID**
    - 16 bytes: unsigned 16-byte big-endian integer.
