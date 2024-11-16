## 2024-11-15

Breaking changes:

- The config file loading workflow has changed. `ggconfigd` can now read from a
  config file and/or a directory of config files, and use it to set default
  values for configuration.

  If config files are available at `/etc/greengrass/config.yaml` or in
  `/etc/greengrass/config.d/`, they will be loaded automatically. The
  `--config-file` and `--config-dir` args let you override these locations.

Known Bugs:

- `ggconfigd` logs error messages if configuration files are not found in
  /etc/greengrass; these error messages are harmless and can be ignored.

Bug Fixes:

- File permissions for deployment-created files is fixed when running the
  Greengrass Lite core services and generic components as different users.
- Greengrass Lite builds when close_range is not available.
- Fixed warning on newer CMake versions.
- Fleet status reports include correct device architecture.
- Greengrass Lite reports runtime field for cloud to identify it as Lite.
- Issue with copying directories during local deployments on some systems is
  fixed.

Features:

- MQTT is used without clean session, and unacked messages with QoS1 are resent
  after disconnects.
- MQTT topic filters are unsubscribed from when all Greengrass clients
  subscribed to it close their Greengrass subscriptions for it.

## 2024-10-22

Bug Fixes:

- `--gc-sections` linker flag was causing misaligned accesses on 32-bit arm. The
  flag has been disabled; this will need to be root caused further.

## 2024-10-21

Breaking changes:

- sqlite autoindex warning fixed. Delete and rebuild your database to get the
  fix.
- Database is now case-sensitive. The sample config is also updated (Ensure your
  initial config yaml file uses rootPath and not rootpath). Your recipes may
  need to be updated to use the correct casing (as in the recipe docs
  <https://docs.aws.amazon.com/greengrass/v2/developerguide/component-recipe-reference.html>)

Features:

- Deployment updates the cloud on failed deployments.
- Failing components will be retried up to three times.
- Deployments are marked successful when all components successfully start.
- SubscribeToConfigurationUpdate IPC command is available.
- Cloud deployments now use local dependencies if available.
- ZIP type artifacts are now unarchived.
- Artifact hashes are now verified.
- Stale components are cleaned up.
- A cloud logging generic component is included.
- MQTT connection reconnects with backoff.
- iotcored unsubscribes from unused topic filters.
- Multiple deployments across thing groups is now supported.
- Recipes now support configuration in lifecycle step variable substitution.
- Fleet status service provides updates of Greengrass device state to cloud.
- Components are only restarted if their version is changed.

Bug Fixes:

- Absolute file paths are no longer logged on old compiler versions.
- ggipcd uses rootPath for creation of the IPC socket.
- TES environment variables are set for generic components.
- Coverity findings are fixed.
- Many minor bugs were fixed.

## 2024-10-04

Features:

- Cloud deployments now support dependencies.
- First party component artifacts can now be downloaded.
- IPC Authz policies are now enforced.
- Recipe variables (other than configuration) can be used.
- DefaultConfiguration from recipes is now applied.
- Now builds against Musl and on 32-bit arm platforms.
- Development container is available under `misc/container` for testing.

Bug Fixes:

- Now builds for older Linux kernels.
- run_nucleus now runs TES.
- Python is no longer needed.
- Fixed bug with SVCUID validation when building with auth.

Known Issues:

- `ggl-config-init` crashes on some platforms (under investigation).
- Errors are logged for sqlite and opening json files; these can be ignored.
- GG user/group in config must be the same user Greengrass is running as.

## 2024-08-26

- Please install fresh, or delete the configuration store config.db file
  (located at the ggconfigd service working directory). This is to avoid
  conflicts with configuration persisted by older versions

Features:

- ggconfigd and GetConfiguration supports reading nested configuration back in a
  single read call
- Fleet Provisioning will now manage its own new instance of iotcored

Bug Fixes:

- TES_server will now support `http://` prefix in config

Known Issues:

- Fleet Provisioning does not terminate even after provisioning is complete will
  fix in future
- If using Fleet Provisioning provide the `iotCredEndpoint` within the
  `Nucleus-Lite`'s config scope
