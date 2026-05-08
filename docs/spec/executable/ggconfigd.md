# `ggconfigd` spec

`ggconfigd` creates a platform-agnostic interface to the configuration system
for Greengrass. `ggconfigd` provides backups, restore, default loading, and
basic read/write access to the key/value store.

- [ggconfigd-1] `ggconfigd` shall maintain a key/value database of configuration
  data.
- [ggconfigd-2] `ggconfigd` shall provide the `gg_config` core-bus interface.
- [ggconfigd-3] `ggconfigd` shall provide corebus interfaces for backup and
  restore to support deployments.
- [ggconfigd-4] `ggconfigd` shall be configured to ensure write operations are
  persisted in the event of unexpected reboots.
- [ggconfigd-5] `ggconfigd` shall provide corebus interfaces for IPC access of
  configuration data.
- [ggconfigd-6] `ggconfigd` shall provide a mechanism to upgrade the datastore
  to newer versions.
- [ggconfigd-7] `ggconfigd` shall load initial configuration from a YAML file
  (default: `/etc/greengrass/config.yaml`) and a config directory (default:
  `/etc/greengrass/config.d/`) at startup, merging values into the database with
  a fixed, low-priority timestamp.
- [ggconfigd-8] `ggconfigd` shall use timestamp-based conflict resolution for
  writes: a write with a timestamp older than the existing value's timestamp
  shall be silently ignored.
- [ggconfigd-9] When a value is written, `ggconfigd` shall notify subscribers on
  the written key and all ancestor keys in the key path.

## Core Bus API: `backup`

Create a backup of the current configuration database using SQLite's backup API.
The backup is stored as `config.db.backup` alongside the live database. Only one
backup is maintained; calling `backup` overwrites any previous backup.

- Takes no parameters.
- Returns `GG_ERR_OK` on success, `GG_ERR_FAILURE` if the database is not
  initialized or the backup operation fails.

## Core Bus API: `restore`

Restore the configuration database from a previously created backup. After
restoring:

1. Stale subscriptions (referencing key IDs that no longer exist in the restored
   database) are removed.
2. All remaining active subscribers are notified so that daemons detect the
   reverted configuration values.

- Takes no parameters.
- Returns `GG_ERR_OK` on success, `GG_ERR_FAILURE` if the database is not
  initialized, the backup file does not exist, or the restore operation fails.
- Note: Subscriber notifications are best-effort. All subscribers are notified
  regardless of whether their key's value actually changed. A future improvement
  could suppress notifications for unchanged values.

## Data Model

The greengrass datamodel is a hierarchical key/value store. Keys are in the form
of paths: `root/path/key = value`. Keys/paths are case insensitive (though they
may be stored internally with case).

Any data is permitted in a value. The data that goes in, is returned when read.

A key that has no value and no children represents an empty map. Writing a
scalar value to a key that is already a map (has children or is an empty map) is
an error. Writing an empty map to a key that already has a scalar value is an
error.

### Error Constants

- ERRORS are part of the GGLITE Core Bus API Error handling.

| Error Name     | Purpose                                                 |
| -------------- | ------------------------------------------------------- |
| GG_ERR_OK      | The command completed successfully                      |
| GG_ERR_FAILURE | The command failed. Check the logs for details          |
| GG_ERR_INVALID | The command parameters are incorrect                    |
| GG_ERR_NOENTRY | The command parameters specified a non-existent entry   |
| GG_ERR_RANGE   | The command parameters exceed a maximum length          |
| GG_ERR_NOMEM   | The command failed due to exceeding memory requirements |

## Component Configuration IPC API

See [the supported IPC commands in the README](../../../README.md).

## Implementation

See [the ggconfigd design](../../design/ggconfigd.md).

## Configuration Core Bus API

The detailed RPC interface is specified in
[gg_config core bus interface](../core-bus-interface/gg_config.md).

## Future Work

The following additions to the core bus API may be added in the future:

### export

Produce a TLOG export of the current configuration and save it to the specified
log file. A TLOG file is a combination of a complete dump of the entire
configuration and the delta to that configuration. For the export only the
complete dump is required.

### import

Import the specified log file, preferring the specified log file where there are
conflicts.

### merge

Merge the specified log file, preferring the newest data where there are
conflicts.
