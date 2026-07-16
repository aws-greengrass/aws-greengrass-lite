# `SubscribeToConfigurationUpdate` IPC command

The `SubscribeToConfigurationUpdate` IPC command subscribes to changes in a
component's configuration through the `gg_config` core-bus interface.

- [subscribe-config-1] It is invoked with the topic
  `SubscribeToConfigurationUpdate`.
- [subscribe-config-2] It subscribes to the requested key and its descendants.
- [subscribe-config-3] A request with `componentName` `aws.greengrass.Nucleus`
  subscribes to the `aws.greengrass.NucleusLite` configuration tree. Events
  retain the requested `aws.greengrass.Nucleus` component name.
  - [subscribe-config-3.1] The alias changes only the component name. It does
    not translate configuration schemas or synthesize Classic nucleus keys.
  - [subscribe-config-3.2] A requested key that is not present in the
    `aws.greengrass.NucleusLite` tree returns `ResourceNotFoundError`, including
    keys that exist only in the Classic nucleus schema.

## Parameters

- [subscribe-config-params-1] `componentName` is an optional string.
  - [subscribe-config-params-1.1] If omitted, the calling component's name is
    used.
- [subscribe-config-params-2] `keyPath` is an optional list of strings.
  - [subscribe-config-params-2.1] Each string identifies one level in the
    configuration hierarchy.
  - [subscribe-config-params-2.2] If omitted or empty, the subscription covers
    the component's entire configuration.

## Response and events

- [subscribe-config-resp-1] On success, the initial response is empty.
- [subscribe-config-resp-2] Each notification delivered by the `gg_config`
  subscription produces a `ConfigurationUpdateEvents` message containing a
  `configurationUpdateEvent` map.
  - [subscribe-config-resp-2.1] `componentName` identifies the requested
    component.
  - [subscribe-config-resp-2.2] `keyPath` contains the path reported by the
    `gg_config` notification.
  - [subscribe-config-resp-2.3] Notification generation and subscription
    lifetime follow the
    [`gg_config` interface](../../core-bus-interface/gg_config.md). A write that
    emits no core-bus notification emits no IPC event. Deleting a key removes
    affected underlying subscriptions without an update event, while restoring a
    backup can notify a subscription even if its value did not change.
- [subscribe-config-resp-3] On failure, `ResourceNotFoundError` is returned when
  the requested path does not exist. Other subscription failures return
  `ServiceError`.

## Example

Nucleus configuration on Greengrass Lite is stored under
`aws.greengrass.NucleusLite`. The following subscription uses the Classic
nucleus component name for a key that is available in the Greengrass Lite
schema:

- Request:
  `{"componentName":"aws.greengrass.Nucleus","keyPath":["iotDataEndpoint"]}`
- Initial response: `{}`
- Update event:
  `{"configurationUpdateEvent":{"componentName":"aws.greengrass.Nucleus","keyPath":["iotDataEndpoint"]}}`
