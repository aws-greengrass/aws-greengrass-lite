## Lifecycle API Requirements

## 1.0.

A plugin shall publish a single entry point for accepting lifecycle events.

### 1.1.

Nucleus shall call the plugin with the lifecycle event `bootstrap` when <XXX>
occurs

### 1.2.

Nucleus shall call the plugin with the lifecycle event `discover` when <XXX>
occurs

### 1.3.

Nucleus shall call the plugin with the lifecycle event `start` when <XXX> occurs

### 1.4.

Nucleus shall call the plugin with the lifecycle event `run` when <XXX> occurs

### 1.5.

Nucleus shall call the plugin with the lifecycle event `terminate` when <XXX>
occurs

## 2.0.

The plugin lifecycle event entry point shall be named `greengrass_lifecycle`

### 2.1.

The first parameter to the lifecycle entry point shall be a uint32_t
`moduleHandle` representing the module handle.

### 2.2.

The second parameter to the lifecycle entry point shall be a uint32_t `phase`
that represents the intern string identifying the lifecycle event name.

### 2.3.

The third parameter to the lifecycle entry point shall be a uint32_t `data` that
represents the data supplied with the lifecycle event.

### 2.4.

The lifecycle entry point shall return a boolean indicating the success of the
plugin responding to the event.

### 2.5.

Nucleus will respond to a `true` response by XXX.

### 2.6.

Nucleus will respond to a `false` response by XXX.
