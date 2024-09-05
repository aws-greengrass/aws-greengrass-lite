# Design for the `ggconfigd` SQLITE implementation

Implementing the [ggconfigd spec](../spec/components/ggconfigd.md), the
ggconfigd implementation uses a relational database (sqlite) to persist the
configuration.

## sqlite dependency

Most systems have sqlite already installed. If necessary, this library can be
built with the sqlite source interated (it is a single giant C file) and all
components can link against this library including the ggconfigd component.

## Data model

The datamodel for gg config is a hierarchical key-value store. All values are
stored as json-encoded strings.

The structure of the configuration hierarchy is as follows:

```
[]                      # configuration root (not part of a key path)
├─ system/                # system configuration section
|  ├─ rootPath              # system configuration values set at startup
|  ├─ thingName
|  ├─ rootCaPath
|  └─ certificateFilePath
└─ services/              # component-specific configurations
   ├─ Nucleus-Lite/         # A reserved component section shared by GG-Lite core components
   │  ├─ iotRoleAlias
   │  ├─ iotDataEndpoint
   |  ├─ iotCredEndpoint
   |  └─ ...
   ├─ user-component-1      # An example user component section
   │  ├─ user-config-a        # An example user configuration value
   │  └─ user-config-b
   └─ user-component-2
      └─ user-config-c
```

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
different ids.

### Relationship Table

```SQL
CREATE TABLE relationTable( 'keyid' INT UNIQUE NOT NULL,
                            'parentid' INT NOT NULL,
                            primary key ( keyid ),
                            foreign key ( keyid ) references keyTable(keyid),
                            foreign key( parentid) references keyTable(keyid));
```

The relationship table allows the keys to keep track of their parents in the
hierarchy. This forms an adjacency list.

### Value Table

```SQL
CREATE TABLE valueTable( 'keyid' INT UNIQUE NOT NULL,
                         'value' TEXT NOT NULL,
                         'timeStamp' TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                         foreign key(keyid) references keyTable(keyid) );
```

The value table keeps the actual value stored at a key with a timestamp and a
link to the key. The timestamp is automatically created when a row is inserted.
An update trigger will update the timestamp automatically when the value is
updated. If an update specifically includes the timestamp the update trigger
will overwrite the value.

> NOTE: The timestamp details will likely be updated soon, in that it will be
> either provided by the caller or have a default generated in the db_interface
> layer. The database should not generate the timestamp itself.

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

### Preventing Duplicate Keys in the DB

A duplicate key is defined as when there are two or more keys (e.g. separate
`keyid`s) which have the same `keyvalue` (see keyTable) as well as the same
`parentid` (see relationTable). The DB schema itself does not prevent this, for
reasons described in
[Validation of operations / Preserving data integrity](#Validation-of-operations-/-Preserving-data-integrity).
The C code prevents this as follows:

1. We ignore any possibility of external modifications or corruptions of the db,
   we assume ggconfigd (and only one instance of ggconfigd) is the only thing
   accessing the db.
1. Duplicates can only be introduced during write
   (`ggconfig_write_value_at_key`).
1. `ggconfig_write_value_at_key` uses a transaction to make its operation
   atomic, so there are no concurrency concerns within its transaction.
1. `ggconfig_write_value_at_key` only creates a key if it doesn't exist.
1. Therefore duplicate keys can not be introduced.

We could add a redundant check inside of `ggconfig_write_value_at_key`, where it
queries the db to check for duplicates before it completes a write transaction.
The value from doing this is that it would prevent future problems as the code
evolves, if one of the premises for why duplicates can't exist now are broken
unknowingly. But the downside is it's not strictly necessary for now and would
have some kind of an impact on performance, doing an extra query involving
joining the key and relation tables together. For these reasons we don't have
such a check currently.

### (Not) Preventing Notification <-> Write Infinite Loops

Consider the case where a component A has subscribed to key 1. Component A, upon
seeing any notification for key 1, is coded to update key 1 in some way. This
causes an infinite loop. In general, this is an error in the component's logic,
and as such we do not mitigate it.

The situation could be helped in the future by preventing a component from
notifying itself on any changes it makes. This is a change to the ggconfigd and
component config IPC spec that could be made after consideration of if it would
break customers. It is technically a breaking change, even if we don't see a use
case for it.

### Grouping Notifications

Grouping notifications is part of the config IPC design, in that one
notification can contain one or many ConfigurationUpdateEvents. Some potential
benefits of doing this are 1. to prevent a configuration update storm, where
updates trigger more updates and clog the notification and update throughput,
and 2. preventing a backlog of notifications that a subscriber has to process
one by one when they could process them more efficiently if received as a group.
Currently notification grouping doesn't happen, and all notifications are sent
for a single write that took place.

Some options for implementing notification grouping are:

1. Have a separate task which sends out notifications. This would be separate
   from the write handler, which currently sends out notifications as part of a
   write operation.
1. See how the V2 Classic nucleus does it, and do something similar.

### Suppressing Notifications when the value is written to but has not changed

If a value is written to the config, but its value doesn't change (although the
timestamp may be updated), there is no reason that a subscriber cares about this
event. Removing notifications for these events helps prevent unnecessary
notifications and reactions to unnecessary notifications. Currently we don't
have this suppression functionality, but it could be added in the future.

### Subscription behavior for keys which become deleted

In GG Classic, if a key is deleted (e.g. via reset config paths during
deployment), then all subscriptions to that key are also deleted. Even if the
key is re-created later, the previous subscriptions are gone and they will not
be notified anymore. This is an undesirable situation for component writers to
have to deal with. The ideal from a component writer perspective would be that
subscriptions can be preserved across periods of key deletion (and perhaps that
you can also subscribe to a key which doesn't exist- because it could exist in
the future).

1. Send a notification for keys that are deleted, then subscribers have to
   re-subscribe and can try periodically (similar to mqtt). A problem to solve
   with this is that the SubscribeToConfigurationUpdate IPC response
   (ConfigurationUpdateEvents) doesn't have fields to indicate such an event
   currently.
2. Allow keys to exist "for subscriber tracking only".
   - A. Have a parallel subscriberKeyTable which is a superset of the actual
     keyTable. Anything added to the actual keyTable will have a mapping to the
     subscriberKeyTable. This would allow subscriptions to be created for
     non-existent keys or maintained for keys which become non-existent.
   - B. Have a temp table for pending subscriptions in addition to the active
     subscriptions
