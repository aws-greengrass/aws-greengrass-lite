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
  to newer versions_

## Data Model

The greengrass datamodel is a hierarchical key/value store. Keys are in the form
of paths: `root/path/key = value`. Keys are case insensitive (though they may be
stored internally with case) and consist of alphabetical characters separated by
a '/' spaces a numbers are not permitted in the keys.

> NOTE verify the key rules of greengrass legacy

Any data is permitted in a value. The data that goes in, is returned when read.

## Core Bus API

### write

Write a value to the indicated key. Create the keypath and key as required.

### read

Read a value from the indicated key. If the key/keypath are not found, return an
error.

### ipc_write

Handle the IPC configuration write command with the IPC parameters. IPC
parameters include:

1. component name
2. complete keypath
3. value to store at the keypath.

All keys stored with ipc_write will be internally mapped to the following
keypath: `ipc/<component>/<keypath> = <value>`

### ipc_read

Handle the IPC configuration read command with the IPC paramameters. IPC
parameters include:

1. component name
2. complete keypath

The internal keypath will be generated from the supplied parameters and the
value stored will be returned.

> NOTE Can a partial path be requested that will return ALL the data below it?
> If so, what is the data format for this combined request?

### snapshot

Create a snapshot of the current configuration and store it with the current
date.

### restore

Restore a previous snapshot. If no date is provided, it will restore the
previous snapshot. If a date is provided then it must exactly match a previous
snapshot.

### snapshot_history

Return a list of previous snapshots and dates.

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

## MERGE RULES

## KEY NAME RULES

##
