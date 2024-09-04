# Component Configuration IPC API

The following commands are a part of the IPC command set and supported by
`ggipcd`. `ggipcd` uses the
[`ggconfigd` Core Bus API](../components/ggconfigd.md) to provide the IPC API
below.

### GetConfiguration

Gets a configuration value for a component on the core device.

#### Parameters

| Parameter name           | Parameter Description                                                                          |
| ------------------------ | ---------------------------------------------------------------------------------------------- |
| componentName (optional) | The name of the component. If no name is provided, the default is the callers name.            |
| keyPath                  | The keyPath is a list where each entry in order is a single level in the configuration object. |

For example, when keyPath is `["mqtt","port"]` the request will return the value
for `mqtt/port`. All values below the specified keyPath will be returned in a
single object. If the keyPath parameter is an empty list, all values for the
component will be returned.

#### Response

| Field         | Description                               |
| ------------- | ----------------------------------------- |
| componentName | The name of the component.                |
| value         | The requested configuration as an object. |

### UpdateConfiguration

Updates a configuration value for this component on the core device.

#### Parameters

| Parameter name | Parameter Description                                                                          |
| -------------- | ---------------------------------------------------------------------------------------------- |
| keyPath        | The keyPath is a list where each entry in order is a single level in the configuration object. |
| timestamp      | The current Unix epoch time in milliseconds, to resolve concurrent updates to the key.         |
| valueToMerge   | The configuration object to merge at the location that you specify in keyPath.                 |

For example, specify the key path `["mqtt"]` and the merge value
`{ "port": 443 }` to to set the value of port in the following configuration:

```
{
  "mqtt": {
    "port": 443
  }
}
```

If the key in the component configuration has a greater timestamp than the
timestamp in the request, then the request makes no change.

If there is a request to write an object with multiple keys, and some of the
keys in the component configuration have a greater timestamp than those keys in
the request, then those keys will be ignored while the other keys are written.

#### Response

This operation doesn't provide any information in its response.

### SubscribeToConfigurationUpdate

Subscribe to receive notifications when a component's configuration updates.
When you subscribe to a key, you receive a notification when any child of that
key updates.

This operation is a subscription operation where you subscribe to a stream of
event messages. To use this operation, define a stream response handler with
functions that handle event messages, errors, and stream closure.

#### Parameters

| Parameter name           | Parameter Description                                                                          |
| ------------------------ | ---------------------------------------------------------------------------------------------- |
| componentName (optional) | The name of the component. If no name is provided, the default is the callers name.            |
| keyPath                  | The keyPath is a list where each entry in order is a single level in the configuration object. |

For example, when keyPath is `["mqtt","port"]` the request will notify the
subscriber any time the value for `mqtt/port` changes. All values below the
specified keyPath will also be notified for. If the keyPath parameter is an
empty list, all values for the component will be notified for.

#### Response

Event message type: ConfigurationUpdateEvents

This operation's response has the following information:

```
messages
    The stream of notification messages. This object, ConfigurationUpdateEvents, contains the following information:

    configurationUpdateEvent (Python: configuration_update_event)
        The configuration update event. This object, ConfigurationUpdateEvent, contains the following information:

        componentName (Python: component_name)
            The name of the component.

        keyPath (Python: key_path)
            The key path to the configuration value that updated.
```
