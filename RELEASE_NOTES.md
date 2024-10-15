## 2024-10-18 (In Progress)

Features:

Bug Fixes:

- sqlite autoindex warning fixed. Delete and rebuild your database to get the
  fix.

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
