# Design for the `ggconfigd` SQLITE implementation

Implementing the [ggconfigd spec](../spec/components/ggconfigd.md), the current
ggconfigd implementation uses a relational database (sqlite) to persist the
configuration.

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
hierarchy. This forms an adjacency list.

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
