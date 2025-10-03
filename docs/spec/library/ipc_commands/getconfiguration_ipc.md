# `GetConfiguration` IPC command

The `GetConfiguration` IPC command retrieves a component's configuration stored
at the nucleus level using the `gg_config` core-bus interface.

- [get-config-1] It can be invoked with the topic `GetConfiguration`.
- [get-config-2] It can get any component's configurations under the `services`
  section.
- [get-config-3] It does not require access control policy in recipe to work.
- [get-config-4] It cannot retrieve any values under the `system` section of
  config.

## Parameters

- [get-config-params-1] `componentName` is an optional parameter of type buffer.
  - [get-config-params-1.1] If not provided, the calling component's name is
    used.
- [get-config-params-2] `keyPath` is an optional parameter of type list.
  - [get-config-params-2.1] List elements are buffers containing a single level
    in the key hierarchy.
  - [get-config-params-2.2] If not provided, will assume the entire component
    configuration was requested.

## Response

- [get-config-resp-1] On success, returns a map containing `componentName` and
  `value` keys.
  - [get-config-resp-1.1] For non-map values, the `value` contains the key
    itself with the retrieved value.
  - [get-config-resp-1.2] For map values, the `value` contains the map contents
    without the key itself.
- [get-config-resp-2] On failure, returns a map containing `message`,
  `_service`, `_message` and `_errorCode`.
  - [get-config-resp-2.1] `ResourceNotFoundError` is returned if the key path
    does not exist.

## Examples

Following are a few examples of how the request and response could look like in
different scenarios based on the configuration as follows:

```
"DefaultConfiguration": {
    "test_str": "hi",
    "sample_map": {
    "key1": "value1",
    "key2_list": [
        "subkey1",
        "subkey2"
    ],
    "key10": 10.0123456,
    "key4": null,
    "key5_map": {
        "subkey1":"subvalue1",
        "subkey2":"subvalue2"
    }
    }
}
```

Case 1: Get Configuration key path with float

- Request: `{"keyPath": ["sample_map", "key10"]}`
- Response:
  `{"componentName":"com.example.ConfigUpdater","value":{"key10":10.0123456}}`

Case 2: Get Configuration key path with list

- Request: `{"keyPath": ["sample_map", "key2_list"]}`
- Response:
  `{"componentName":"com.example.ConfigUpdater","value":{"key2_list":["subkey1","subkey2"]}}`

Case 3: Get Configuration key path with map

- Request: `{"keyPath": ["sample_map", "key5_map"]}`
- Response:
  `{"componentName":"com.example.ConfigUpdater","value":{"subkey1":"subvalue1","subkey2":"subvalue2"}}`

  > Note that the key isn't included with the response

Case 4: Get Configuration key path with string

- Request: `{"keyPath": ["sample_map", "key5_map", "subkey1"]}`
- Response:
  `{"componentName":"com.example.ConfigUpdater","value":{"subkey1":"subvalue1"}}`

Case 5: Get Configuration key path with null

- Request:`{"keyPath": ["sample_map", "key4"]}`
- Response:`{"componentName":"com.example.ConfigUpdater","value":{"key4":null}}`

Case 6: Get Configuration key path does not exist.

- Request:
  `{"componentName": "com.example.ConfigUpdater", "keyPath": ["randomKey"]}`
- Response:
  `{"message":"Key not found","_service":"aws.greengrass#GreengrassCoreIPC","_message":"Key not found","_errorCode":"ResourceNotFoundError"}`

RAW:

```shell
Packet from client 9:
  Headers:
    [:content-type] => String(StrBytes { bytes: b"application/json" })
    [service-model-type] => String(StrBytes { bytes: b"aws.greengrass#GetConfigurationRequest" })
    [:message-type] => Int32(0)
    [:message-flags] => Int32(0)
    [:stream-id] => Int32(1)
    [operation] => String(StrBytes { bytes: b"aws.greengrass#GetConfiguration" })
  Value: {"componentName": "com.example.ConfigUpdater", "keyPath": ["randomKey"]}
Packet from server:
  Headers:
    [:content-type] => String(StrBytes { bytes: b"application/json" })
    [service-model-type] => String(StrBytes { bytes: b"aws.greengrass#ResourceNotFoundError" })
    [:message-type] => Int32(1)
    [:message-flags] => Int32(2)
    [:stream-id] => Int32(1)
  Value: {"message":"Key not found","_service":"aws.greengrass#GreengrassCoreIPC","_message":"Key not found","_errorCode":"ResourceNotFoundError"}
```
