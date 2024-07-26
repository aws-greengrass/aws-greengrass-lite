# ggconfiglib design

## introduction

The ggconfiglib interfaces a component to the configuration system with a common
API. The configuration system is split into a library and a component to enable
flexibility in implementation.

## Requirements

1. [ggconfiglib-1] The configuration library can retrieve values assigned to
   keys
   - [ggconfiglib-1.1] The library will return GGL_ERR_FAILURE when the
     requested key is not found.
   - [ggconfiglib-1.2] The library will return GGL_ERR_FAILURE when the
     requested keypath is invalid.
   - [ggconfiglib-1.3] The library will return GGL_ERR_FAILURE when the
     requested component is not found.
   - [ggconfiglib-1.4] The library will return GGL_ERR_OK and the requested
     value string when the key is found.
2. [ggconfiglib-2] The library can insert new key/value pairs
   - [ggconfiglib-2.1] The library will create the entire path as needed to
     place the new key-value pair.
   - [ggconfiglib-2.2] The library will return GGL_ERR_FAILURE if the new key is
     a duplicate.
   - [ggconfiglib-2.3] The library will return GGL_ERR_OK when the new value is
     created.
3. [ggconfiglib-3] The library can modify existing key/value pairs
   - [ggconfiglib-3.1] The library will return GGL_ERR_FAILURE when the
     requested key is not found.
   - [ggconfiglib-3.2] The library will return GGL_ERR_FAILURE when the
     requested keypath is invalid.
   - [ggconfiglib-3.3] The library will return GGL_ERR_FAILURE when the
     requested component is not found.
   - [ggconfiglib-3.4] The library will return GGL_ERR_OK when the existing
     value is updated.
4. [ggconfiglib-4] The library can call callbacks when key values change.
   - [ggconfiglib-4.1] The library will return GGL_ERR_FAILURE if the requested
     subscription key is not found.
   - [ggconfiglib-4.2] The library will return GGL_ERR_FAILURE when the
     requested keypath is invalid.
   - [ggconfiglib-4.3] The library will return GGL_ERR_FAILURE when the
     requested component is not found.
   - [ggconfiglib-4.4] The library will return GGL_ERR_OK when the subscription
     callback is installed.
   - [ggconfiglib-4.5] The library will accept a NULL callback reference to
     disable notifications.
5. [ggconfiglib-5] valid key rules
   - [ggconfiglib-5.1] A key is either a leaf or a branch. Leaf's contain data
     while branches are links between branches or to leaves.
   - [ggconfiglib-5.2] A key is named as

## Library API

The API follows CRU. Create, Read, Update. Note the DELETE is NOT supported in
this version.

### Functions

| function                    | purpose                                           | parameters                      |
| --------------------------- | ------------------------------------------------- | ------------------------------- |
| ggconfig_open               | open the configuration system                     | None                            |
| ggconfig_close              | close the configuration system                    | None                            |
| ggconfig_write_value_at_key | create a key with the indicated value.            | Key, Value                      |
| ggconfig_get_value_from_key | Return the value stored at the specified keypath. | Key, Value, Value Buffer Length |
| ggconfig_getKeyNotification | Register a callback on a keypath                  | Key, Callback                   |

#### ggconfig_open

Open the configuration system for access. The return will be GGL_ERR_OK or
GGL_ERR_FAILURE.

#### ggconfig_close

Close the configuration system for access. The return will be GGL_ERR_OK or
GGL_ERR_FAILURE.

#### ggconfig_insert_key_and_value

The insert_key_and_value function will inspect the provided key path and
determine that the key does not already exist. If the path does not exist it
will create the keys in the path and add the data in the last key. If the path
already exists it will return GG_ERR_FAILURE.

#### ggconfig_update_value_at_key

The update_value_at_key function will find an existing key in the database and
update the value to the new value supplied.

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

This implementation will use an path list to create the hierarchical data
mapping. The table needed for this configuration is as follows:

1. Path Table
2. Relationship Table
3. Value Table
4. Version Table

### Path Table

```sql
CREATE TABLE pathTable('pathid' INTEGER PRIMARY KEY AUTOINCREMENT UNIQUE NOT NULL,
                       'pathvalue' TEXT NOT NULL UNIQUE COLLATE NOCASE  );
```

The pathTable keeps a list of every path segment in the system. The path
'foo/bar/baz' will result in 3 entries into the path table: 'foo', 'foo/bar' and
'foo/bar/baz' with three different id's.

### Relationship Table

```SQL
CREATE TABLE relationTable( 'pathid' INT UNIQUE NOT NULL,
                            'parentid' INT NOT NULL,
                            PRIMARY KEY ( pathid ),
                            FOREIGN KEY ( pathid ) REFERENCES pathTable(pathid),
                            FOREIGN KEY( parentid) REFERENCES pathTable(pathid));
```

The relationship table allows the paths to keep track of their parents in the
hierarchy. This allows a query such as:

```SQL
SELECT V.Value FROM relationTable R LEFT JOIN valueTable V LEFT JOIN pathTable P WHERE
  P.PathId = V.PathID AND P.PathID = R.PathID AND pathvalue = 'foo/bar/baz';
```

Which will ensure the returned value is a entry with a specific path and
parentage. The table creation rules also prevent duplicate keys by keeping the
path and parents unique.

### Value Table

```SQL
CREATE TABLE valueTable( 'pathid' INT UNIQUE NOT NULL,
                         'value' TEXT NOT NULL,
                         'timeStamp' TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                         FOREIGN KEY(pathid) REFERENCES pathTable(pathid) );
```

The value table keeps the actual value stored at a path with a time stamp and a
link to the path. The Timestamp is automatically created when a row is inserted.
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

### Appendix Other hierarchical map techniques

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
