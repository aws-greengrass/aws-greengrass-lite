# `ggconfigd` spec

`ggconfigd` creates a platform-agnostic interface to the configuration system
for Greengrass. `ggconfigd` provides snapshot, rollback, default loading, tlog
import/export and basic read/write access to the key/value store.

- [ggconfigd-1] `ggconfigd` shall maintain a key/value database of configuration
  data.
- [ggconfigd-2] `ggconfigd` shall provide corebus interfaces for read/write of
  configuration data.
- [ggconfigd-3] `ggconfigd` shall provide corebus interfaces for snapshots and
  rollback to support deployments.
- [ggconfigd-4] `ggconfigd` shall provide corebus interfaces for tlog
  import/export to support GG migration.
- [ggconfigd-5] `ggconfigd` shall be configured to ensure write operations are
  persisted in the event of unexpected reboots.
- [ggconfigd-6] `ggconfigd` shall provide corebus interfaces for IPC access of
  configuration data.
- _[ggconfigd-7] `ggconfigd` shall provide a mechanism to upgrade the datastore
  to newer versions._

## Data Model

The greengrass datamodel is a hierarchical key/value store. Keys are in the form
of paths: `root/path/key = value`. Keys / Paths are case insensitive (though
they may be stored internally with case).

Any data is permitted in a value. The data that goes in, is returned when read.

## Core Bus API

### write

Write a value to the indicated key. Create the key_path and key as required.

| Parameter name | Parameter Description                                                                           |
| -------------- | ----------------------------------------------------------------------------------------------- |
| key_path       | The key_path is a list where each entry in order is a single level in the configuration object. |
| value          | The configuration object to merge at the location that you specify in key_path.                 |
| timestamp      | The current Unix epoch time in milliseconds, to resolve concurrent updates to the key.          |

If the key in the component configuration has a greater timestamp than the
timestamp in the request, then the request makes no change.

If there is a request to write an object with multiple keys, and some of the
keys in the component configuration have a greater timestamp than those keys in
the request, then those keys will be ignored while the other keys are written.

### read

Read a value from the indicated key. If the key is not found, return an error.

| Parameter name | Parameter Description                                                                           |
| -------------- | ----------------------------------------------------------------------------------------------- |
| key_path       | The key_path is a list where each entry in order is a single level in the configuration object. |

### subscribe

Subscribe to updates when the indicated key changes. If the key is not found,
return an error.

| Parameter name | Parameter Description                                                                           |
| -------------- | ----------------------------------------------------------------------------------------------- |
| key_path       | The key_path is a list where each entry in order is a single level in the configuration object. |

### snapshot

Create a snapshot of the current configuration and store it with the current
date.

> NOTE: The snapshot, restore, snapshot_history api may be simplified to a
> simple api such as create_snapshot and restore with no parameters

### restore

Restore a previous snapshot. If no date is provided, it will restore the
previous snapshot. If a date is provided then it must exactly match a previous
snapshot.

### snapshot_history

Return a list of previous snapshots and dates.

### Error Constants

- ERRORS are part of the GGLITE Core Bus API Error handling.

| Error Name      | Purpose                                               |
| --------------- | ----------------------------------------------------- |
| GGL_ERR_OK      | The command completed successfully                    |
| GGL_ERR_FAILURE | The command failed. Check the logs for details        |
| GGL_ERR_INVALID | The command parameters are incorrect                  |
| GGL_ERR_NOENTRY | The command parameters specified a non-existent entry |

## Component Configuration IPC API

See [the Component Configuration IPC API spec](../ipc/component-config.md).

## Implementation

See [the ggconfigd design](../../design/ggconfigd.md).

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
