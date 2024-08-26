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

Write a value to the indicated key. Create the key_path and key as required.

| Parameter name | Parameter Description                                                             |
| -------------- | --------------------------------------------------------------------------------- |
| component      | The name of the component. Used as the first entry in the key path                |
| key            | The key_path that is appended to the component name and addresses a piece of data |
| value          | The data value written to the component/key                                       |

### read

Read a value from the indicated key. If the key/key_path are not found, return
an error.

| Parameter name | Parameter Description                                                             |
| -------------- | --------------------------------------------------------------------------------- |
| component      | The name of the component. Used as the first entry in the key path                |
| key            | The key_path that is appended to the component name and addresses a piece of data |

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

## IPC API

The following commands are a part of the IPC command set and supported by
ggconfigd.

### GetConfiguration

Gets a configuration value for a component on the core device. You specify the
key path for which to get a configuration value.

#### Parameters

> componentName (optional) : The name of the component. If no name is provided,
> the default is the callers name. key_path : The key_path to the configuration
> value. The key_path is a list where each entry in order is a value in the
> path. example: ["mqtt","port"] will return the value for `mqtt/port`. All
> values below the identified point in the key_path will be returned in a single
> `GglObject`. If the parameter is an empty list, all values for the componet
> will be returned.

#### Response

> componentName : The name of the component value : The requested configuration
> as an object.

### UpdateConfiguration

### SubscribeToConfigurationUpdate

## MERGE RULES

## KEY NAME RULES

### Error Constants

- ERRORS are part of the GGLITE Error handling.

| Error Name      | Purpose                                        |
| --------------- | ---------------------------------------------- |
| GGL_ERR_OK      | The command completed successfully             |
| GGL_ERR_FAILURE | The command failed. Check the logs for details |
| GGL_ERR_INVALID | The command parameters are incorrect           |

## Design for SQLITE implementation

The design before this point should be universal to any implementation of the
ggconfiglib. Below this point is a specific implementation suitable for a
relational database such as sqlite.

## sqlite dependency

Most systems have sqlite already installed. If necessary, this library can be
built with the sqlite source interated (it is a single giant C file) and all
components can link against this library including the ggconfigd component.

## Data model

The datamodel for gg config is a hierarchical key-value store. Each config key
is "owned" by a component. All values are stored as strings.

## Mapping the Datamodel to a relational database (sqlite)

This implementation will use a path list to create the hierarchical data
mapping. The table needed for this configuration is as follows:

1. Key Table
2. Relationship Table
3. Value Table
4. Version Table

### Key Table

```sql
CREATE TABLE keyTable('keyid' INTEGER PRIMARY KEY AUTOINCREMENT unique not null,
                      'keyvalue' TEXT NOT NULL UNIQUE COLLATE NOCASE  );
```

The keyTable keeps a list of every key in the system. The path 'foo/bar/baz'
will result in 3 entries into the key table: 'foo', 'bar' and 'baz' with three
different id's.

### Relationship Table

```SQL
CREATE TABLE relationTable( 'keyid' INT UNIQUE NOT NULL,
                            'parentid' INT NOT NULL,
                            primary key ( keyid ),
                            foreign key ( keyid ) references keyTable(keyid),
                            foreign key( parentid) references keyTable(keyid));
```

The relationship table allows the keys to keep track of their parents in the
hierarchy. This allows a query such as:

// TODO: update with new query

```SQL
SELECT V.value FROM relationTable R LEFT JOIN valueTable V LEFT JOIN keyTable K WHERE
  K.keyid = V.keyid AND K.keyid = R.keyid AND keyvalue = 'baz';
```

Which will ensure the returned value is a entry with a specific path and
parentage. The table creation rules also prevent duplicate keys by keeping the
path and parents unique.

### Value Table

```SQL
CREATE TABLE valueTable( 'keyid' INT UNIQUE NOT NULL,
                         'value' TEXT NOT NULL,
                         'timeStamp' TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                         foreign key(keyid) references keyTable(keyid) );
```

The value table keeps the actual value stored at a key with a time stamp and a
link to the key. The Timestamp is automatically created when a row is inserted.
An update trigger will update the timestamp automatically when the value is
updated. If an update specifically includes the timestamp the update trigger
will overwrite the value.

### Version Table

```SQL
CREATE TABLE version('version' TEXT DEFAULT '0.1');
```

The version table is a simple table that holds the current schema version. When
future changes demand an update to the schema, the version will allow a
migration algorithm to be created that will update the schema to any future
schema.

### Other hierarchical map techniques

Mapping methods include[^1]:

| Design           | Table count | Query Child | Query Subtree | Delete Key | Insert Key | Move Subtree | Referential Integrity |
| ---------------- | ----------- | ----------- | ------------- | ---------- | ---------- | ------------ | --------------------- |
| Adjacency List   | 1           | easy        | hard          | easy       | easy       | easy         | yes                   |
| Path Enumeration | 1           | hard        | easy          | easy       | easy       | easy         | no                    |
| Nested Sets      | 1           | hard        | easy          | hard       | hard       | hard         | no                    |
| Closure Table    | 2           | easy        | easy          | easy       | easy       | easy         | yes                   |

[^1]:
    This table comes from the following slide deck :
    https://www.slideshare.net/slideshow/models-for-hierarchical-data/4179181#69

Each of these comes with complexities. The adjacency list is small while keeping
the child query, key insert and key delete easy. Query of subtrees is not in the
GG API.

### Validation of operations / Preserving data integrity

Several rules must be enforced on the data inserted into the database so that a
corrupt state does not form. This often involves cross-checking data that is
inserted into multiple tables in related inserts(such as a transaction); in such
cases multiple possible solutions were explored:

- SQL Checks / Constraints don't work because the expression can't contain a
  subquery, and a subquery is needed to get information from another table, and
  this type of check needs information joined from two tables.
- SQL Triggers don't work for validating this either because we need to validate
  the state of things after multiple inserts in different tables have happened.
  But a trigger is defined for operations that happen on one table- not multiple
  tables.
- SQL Stored Procedures are a common solution to a problem like this, where the
  stored procedure abstracts the multiple table operations into a single
  procedure call. However SQLITE3 doesn't support stored procedures.
- We believe the only remaining option is doing this type of rule enforcement in
  the C code. We use the transactions to abort a series of operations if one of
  the inserts fails for some reason, or if at any point we detect that the
  transaction would corrupt the db.
