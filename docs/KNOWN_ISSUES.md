# Greengrass Lite Known Issues List

The following are some known issues in the Greengrass Lite software where
behavior may be different from the classic version of Greengrass. Upcoming or
missing features are not included in this list. This list will be updated as
issues are discovered or fixed.

### Stale phase cleanup does not occur

In Greengrass Lite, phases that become stale when updating a component version
are not cleaned up. For example, a component that includes both an install and a
run lifecycle phase is updated to no longer have an install phase. This install
phase will not be cleaned up and will still be processed even after a component
update has removed it from the recipe. Customers should avoid removing lifecycle
phases when possible, but adding new phases is okay and behaves as expected.

Workaround: The deployment should be revised to remove the component first, and
then can be revised to include it again.

### platformOverride NucleusLite configuration key exists but is not fully supported; architecture.detail can only be set via this configuration

The platformOverride key in Greengrass classic is used to specify a platform
override which can include any platform attributes such as “os” or
“architecture”. The platformOverride key in Greengrass Lite only supports the
“architecture.detail” platform attribute, to be used to set the expected
architecture.detail attribute. Greengrass Lite will not check the system for its
actual architecture detail and solely rely on this configuration value to set
the architecture.detail platform attribute.

Workaround: Only use platformOverride to set architecture.detail. If you want to
deploy a component that specifies an architecture.detail platform attribute,
then the correct value must be set for /platformOverride/architecture.detail in
the NucleusLite configuration.

### Components with a lifecycle step that varies depending on recipe variable substitution will not be restarted if the recipe variable changes

In Greengrass classic, components will be restarted if the “run” lifecycle phase
changes after a deployment. This may include run lifecycle phases that use
variable interpolation to include something from the component configuration in
the lifecycle script. Greengrass Lite will not detect that a component lifecycle
phase has changed in the event of a configuration change changing the recipe
variable used in the lifecycle script, so these components will not restart if
the version has not changed.

Workaround: Components should use the SubscribeToConfigurationUpdate IPC command
to subscribe to configuration changes and update their process to use new
values. If this is not possible, an easier workaround may be to simply bump the
component version in order to force a component restart.

### Greengrass deployments will fail after the same component is deployed locally over 100 times

This is due to a memory limitation in our current design.

Workaround: Avoid deploying the same component name over 100 times locally. If
needed, the component can be renamed which will be under a different counter.

### Certain complex accessControl policies may not properly grant authorization

accessControl policies for MQTT will not support the `#` or `+` MQTT wildcard
substitutions in the same policy including a normal `*` wildcard (the MQTT
wildcards can still be used if the policy does not include a `*`). accessControl
policies will not support the `${*}`, `${?}`, and `${$}` escape sequences
either. They also do not support recipe variable interpolations in policies due
to Greengrass Lite not supporting recipe variable interpolation in configuration
yet and accessControl policies are part of the component configuration.

Workaround: Use simpler authorization policies that do not make use of these
features, referencing the docs here:
https://docs.aws.amazon.com/greengrass/v2/developerguide/interprocess-communication.html

### The root path must be /var/lib/greengrass

Please ensure that Greengrass Lite is installed to the /var/lib/greengrass root
path. This is a limitation with no workaround at the moment.
