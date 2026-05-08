# `gg_config` interface

The `gg_config` core-bus interface provides functionality for managing the
Greengrass installation's configuration. The configuration follows JSON
structure.

Each method in the interface is described below.

## read

The `read` method returns the value associated with `key_path`. If the key has
subkeys, the value is an object with the key-value pairs under the key, which
are read recursively.

- [gg-config-read-1] `read` can be invoked with call.
- [gg-config-read-2] `read` on an object key returns an object encoding the
  hierarchy under that key.

### Parameters

- [gg-config-read-params-1] `key_path` is a required parameter of type list.
  - [gg-config-read-params-1.1] list elements are buffers containing a single
    level in the key hierarchy.

### Response

- [gg-config-read-resp-1] The response value is the value that was stored in the
  database for the key.
- [gg-config-read-resp-2] The method will error if a response value is not
  provided.
  - [gg-config-read-resp-2.1] `GG_ERR_NOENTRY` will be returned if the key was
    not in the configuration.

## list

The `list` method returns a list of immediate subkeys of an object at the given
`key_path`. Unlike `read`, this method does not recursively fetch all nested
keys and values.

- [gg-config-list-1] `list` can be invoked with call.
- [gg-config-list-2] `list` returns only the immediate subkeys of the object
  specified at the key path.

### Parameters

- [gg-config-list-params-1] `key_path` is a required parameter of type list.
  - [gg-config-list-params-1.1] list elements are buffers containing a single
    level in the key hierarchy.

### Response

- [gg-config-list-resp-1] The response value is a list of buffers, where each
  buffer is an immediate subkey of the specified `key_path` object.
- [gg-config-list-resp-2] If the specified `key_path` does not exist,
  `GG_ERR_NOENTRY` is returned.
- [gg-config-list-resp-3] If the specified `key_path` exists but is not a
  object/map (i.e. keyPath directly holds a value), `GG_ERR_INVALID` is
  returned.

## write

The `write` method updates the value associated with `key_path`. If the `value`
is an object with subkeys, it is merged in recursively. Any values (i.e. leaves
of the `value` object) are updated if the new timestamp is greater or equal to
an existing value's timestamp.

- [gg-config-write-1] `write` can be invoked with call or notify.

### Parameters

- [gg-config-write-params-1] `key_path` is a required parameter of type list.
  - [gg-config-write-params-1.1] list elements are buffers containing a single
    level in the key hierarchy.
- [gg-config-write-params-2] `value` is a required parameter of any type.
  - [gg-config-write-params-2.1] If `value` is a map/object, it will be
    recursively merged at `key_path`.
  - [gg-config-write-params-2.2] If `value` is any other type (buffer, integer,
    float, boolean, list), it is JSON-encoded and stored as a leaf value at
    `key_path`.
- [gg-config-write-params-3] `timestamp` is an optional parameter of type int.
  - [gg-config-write-params-3.1] `timestamp` is the Unix epoch time in ms to use
    to compare against existing values.
  - [gg-config-write-params-3.2] If the value is older than an existing key it
    would overwrite, then the old value is kept instead.
  - [gg-config-write-params-3.3] If not provided, the current time is used.

### Response

The `write` method does not have a response value.

- [gg-config-write-resp-1] If the method returns without an error, the
  configuration has been successfully updated.
- [gg-config-write-resp-2] If `value` is a scalar type, but the key is already a
  map, `GG_ERR_FAILURE` is returned.
- [gg-config-write-resp-3] If `value` is an empty map, but the key is already a
  scalar value, `GG_ERR_FAILURE` is returned.

### Notifications

- [gg-config-write-notify-1] On a successful value write, subscribers on the
  written key and all ancestor keys in the path are notified.
  - [gg-config-write-notify-1.1] Writing an empty map does not trigger
    subscriber notifications.
  - [gg-config-write-notify-1.2] A write that is silently ignored due to
    timestamp conflict does not trigger subscriber notifications.

## delete

The `delete` method removes the key and value associated with `key_path`, if
present. If the value is a map with subkeys, everything under it is also deleted
recursively. If the key does not exist, nothing happens.

- [gg-config-delete-1] `delete` can be invoked with call or notify.

### Parameters

- [gg-config-delete-params-1] `key_path` is a required parameter of type list.
  - [gg-config-delete-params-1.1] list elements are buffers containing a single
    level in the key hierarchy.

### Response

The `delete` method does not have a response value.

- [gg-config-delete-resp-1] If the method returns without an error, the
  configuration key does not exist, either because it was deleted or it didn't
  exist when the call was made.

### Notifications

- [gg-config-delete-notify-1] Deleting a key also removes all subscriptions on
  that key and its descendants.
- [gg-config-delete-notify-2] Deleting a key does not trigger subscriber
  notifications.

## subscribe

The `subscribe` method sets up a subscription to updates to the value associated
with `key_path`.

- [gg-config-subscribe-1] `subscribe` can be invoked with call.

### Parameters

- [gg-config-subscribe-params-1] `key_path` is a required parameter of type
  list.
  - [gg-config-subscribe-params-1.1] list elements are buffers containing a
    single level in the key hierarchy.

### Response

- [gg-config-subscribe-resp-1] Subscription responses are sent on each update.
  - [gg-config-subscribe-resp-1.1] The response value is the key path which was
    updated. This may be a child of the `key_path` parameter.
- [gg-config-subscribe-resp-2] The method will return an error if the
  subscripion is not set up.
- [gg-config-subscribe-resp-3] `GG_ERR_NOENTRY` will be returned if the key was
  not in the configuration.

## backup

The `backup` method creates a backup of the current configuration database. The
backup is stored alongside the live database. Only one backup is maintained;
calling `backup` overwrites any previous backup.

- [gg-config-backup-1] `backup` can be invoked with call.

### Parameters

None.

### Response

The `backup` method does not have a response value.

- [gg-config-backup-resp-1] If the method returns without an error, the backup
  has been successfully created.
- [gg-config-backup-resp-2] `GG_ERR_FAILURE` is returned if the database is not
  initialized or the backup operation fails.

## restore

The `restore` method restores the configuration database from a
previously-created backup. After restoring, stale subscriptions (referencing key
IDs that no longer exist) are removed, and all remaining active subscribers are
notified.

- [gg-config-restore-1] `restore` can be invoked with call.

### Parameters

None.

### Response

The `restore` method does not have a response value.

- [gg-config-restore-resp-1] If the method returns without an error, the
  configuration has been successfully restored.
- [gg-config-restore-resp-2] `GG_ERR_FAILURE` is returned if the database is not
  initialized, the backup file does not exist, or the restore operation fails.
- [gg-config-restore-resp-3] After a successful restore, all active subscribers
  are notified with their subscribed key path regardless of whether the value
  actually changed.
