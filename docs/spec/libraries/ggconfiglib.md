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
   - [ggconfiglib-2.3] The library will return GGL_ERR_OK when the new
     value is created.
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
   - [ggconfiglib-5.1] A key is either a leaf or a branch.  Leaf's contain data while branches are links between branches or to leaves.
   - [ggconfiglib-5.2] A key is named as

## Library API
The API follows CRU.  Create, Read, Update.  Note the DELETE is NOT supported in this version.

### Functions

| function                      | purpose                                               | parameters                      |
| ----------------------------- | ----------------------------------------------------- | ------------------------------- |
| ggconfig_open                 | open the configuration system                         | None                            |
| ggconfig_close                | close the configuration system                        | None                            |
| ggconfig_insert_key_and_value | Create a new key in the keypath and add the value.    | Key, Value                      |
| ggconfig_get_value_from_key   | Return the value stored at the specified keypath.     | Key, Value, Value Buffer Length |
| ggconfig_update_value_at_key  | Update the value at the specified key in the keypath. | Key, Value                      |
| ggconfig_getKeyNotification   | Register a callback on a keypath                      | Key, Callback                   |

#### ggconfig_open

Open the configuration system for access.  The return will be GGL_ERR_OK or GGL_ERR_FAILURE.

#### ggconfig_close

Close the configuration system for access.  The return will be GGL_ERR_OK or GGL_ERR_FAILURE.

#### ggconfig_insert_key_and_value

The insert_key_and_value function will inspect the provided key path and determine that the key does not already exist.  If the path does not exist it will create the keys in the path and add the data in the last key.  If the path already exists it will return GG_ERR_FAILURE.

#### ggconfig_update_value_at_key

The update_value_at_key function will find an existing key in the database and update the value to the new value supplied.

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

1. Configuration Table

### Configuration Table

The configuration table includes the owning component and the config parent to
create the hierarchy. The key is a text string and is required to be a non-null
value. The value can be null to allow the value to simply be a "key" on the
hierarchy path.

```
create table config('path' text not null unique COLLATE NOCASE, 
                    'isValue' int default 0,
                    'value' text not null default '',
                    'parent'  text not null default '' COLLATE NOCASE,
                    primary key(path),
                    foreign key(parent) references config(path) );
```                    

| path       | isValue | value         | parent            |
| ---------- | ------- | ------------- | ----------------- |
| TEXT KEY   | INTEGER | TEXT          | TEXT Foreign Key  |

PATH : This is the string path that ends with the key.  The path is case insensitive.
Example - root/path/key

isValue: This is simply an indicator that the path is a full path terminating in a key or it is a partial path that does not end in a key with a value.
TODO: Can isValue go away?

VALUE : The value is the text data that is associated with a config key. The
value can be NULL for keys that only exist in the path with no data. There are
no data types or format checks on the values.

PARENT: This is the full path up to the key without including the key.  This is included to speed the location of items at the same path level.
TODO: Can parent go away.

### Component data owners

The component name should be the first key in a heirarchy.  This allows
duplicate key to be in the list under different components.

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