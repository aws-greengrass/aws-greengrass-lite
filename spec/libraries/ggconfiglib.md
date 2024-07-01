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
   - [ggconfiglib-2.2] The library will return GGL_ERR_FAILURE when the
     requested component is not found.
   - [ggconfiglib-2.3] The library will return GGL_ERR_FAILURE if the new key is
     a duplicate.
   - [ggconfiglib-2.4] The library will return GGL_ERR_FAILURE when the new
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
4. [ggconfiglib-4] The library can add components to the configuration
   - [ggconfiglib-4.1] The library will return GGL_ERR_FAILURE if the component
     is already in the list.
   - [ggconfiglib-4.2] The library will return GGL_ERR_OK when the new component
     is added.
5. [ggconfiglib-5] The library can delete components from the configuration
   - [ggconfiglib-5.1] The library will return GGL_ERR_FAILURE when the
     requested component is not found.
   - [ggconfiglib-5.2] The library will return GGL_ERR_OK when the component is
     deleted.
6. [ggconfiglib-6] The library can call callbacks when key values change.
   - [ggconfiglib-6.1] The library will return GGL_ERR_FAILURE if the requested
     subscription key is not found.
   - [ggconfiglib-6.2] The library will return GGL_ERR_FAILURE when the
     requested keypath is invalid.
   - [ggconfiglib-6.3] The library will return GGL_ERR_FAILURE when the
     requested component is not found.
   - [ggconfiglib-6.4] The library will return GGL_ERR_OK when the subscription
     callback is installed.
   - [ggconfiglib-6.5] The library will accept a NULL callback reference to
     disable notifications.

## Library API

### Functions

| function                    | purpose                                               | parameters                                      |
| --------------------------- | ----------------------------------------------------- | ----------------------------------------------- |
| ggconfig_writeValueToKey    | Update the value at the specified key in the keypath. | Key String, Value String, Component Name String |
| ggconfig_insertKeyAndValue  | Create a new key in the keypath and add the value.    | Key String, Value String, Component Name String |
| ggconfig_getValueFromKey    | Return the value stored at the specified keypath.     | Key String, Value Buffer, Value Buffer Length, Component Name String |
| ggconfig_insertComponent    | Add a component to the component list                 | Component Name String                           |
| ggconfig_deleteComponent    | Remove a component from the component list            | Component Name String                           |
| ggconfig_getKeyNotification | Register a callback on a keypath                      | Key String, Component Name String, Callback     |

### Error Constants

- ERRORS are part of the GGLITE Error handling.

| Error Name      | Purpose                                        |
| --------------- | ---------------------------------------------- |
| GGL_ERR_OK      | The command completed successfully             |
| GGL_ERR_FAILURE | The command failed. Check the logs for details |

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

This implementation will use an adjacency list to create the hierarchical data
mapping. The tables needed for configuration are as follows:

1. Component Table
2. Configuration Table

### Component Table

The component table contains the list of components deployed into the system.
This table can be extended as needed as the deployment system develops.
Component data access is not defined in the GG IPC API.

| Component ID | Component Name |
| ------------ | -------------- |
| Integer KEY  | TEXT           |

COMPONENT ID : The component id is the unique integer value for this component
table row.

COMPONENT NAME : The component name is a text field containing the name for this
component. This name must match the values send via the IPC API to ensure the
correct key-value is accessed.

### Configuration Table

The configuration table includes the owning component and the config parent to
create the hierarchy. The key is a text string and is required to be a non-null
value. The value can be null to allow the value to simply be a "key" on the
hierarchy path.

| Configuration ID | Component Owner ID | Configuration Parent ID | Key           | Value |
| ---------------- | ------------------ | ----------------------- | ------------- | ----- |
| Integer KEY      | Integer            | Integer                 | Text NOT NULL | Text  |

CONFIGURATION ID : The configuration id is the unique integer value for this
configuration key. This allows two configuration items to share the same key
text.

COMPONENT OWNER ID : The component owner id is an integer reference to a
component in the component table.

CONFIGURATION PARENT ID : Each key knows its parent. This creates the hierarchy.
If the parent is NULL then it is the root level of the hierarchy.

KEY : The key is the name associated with this config option. A component must
not have two keys with the same name at the same layer in the hierarchy. All
keys must have a name and cannot be NULL.

VALUE : The value is the text data that is associated with a config key. The
value can be NULL for keys that only exist in the path with no data. There are
no data types or format checks on the values.

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
