## 2024-04-12

- `-DGGLITE_PLATFORM=posix` can be passed to cmake to build for 32-bit systems
  and/or older kernels.
- Greengrass Lite devices now show up on the AWS IoT console web interface under
  Manage > Greengrass devices > Core devices, alongside Greengrass Java devices.
- Generic components' lifecycles and recipe steps are handled by the
  `gen_component_loader` plugin.
- Bundling GG Lite dynamic dependencies now works correctly on systems that use
  lib64 by default instead of lib.

Known bugs:

- Extending a recipe’s script section using default configuration is currently
  not supported

## 2024-03-29

- Fixed bug causing hang when loading plugins in certain optimized builds.
- Installables now also have RUNPATH set in addition to RPATHs.
- OpenSSL's libcrypto is used when available; aws-lc is now the fallback.
- Debuginfo is now compressed in Debug and RelWithDebInfo builds.
- All plugins now have RPATH/RUNPATH set.
- Additional env variables are set for generic components.
- Plugins can now load in dependency order.
- Script timeouts are now handled.
- Runtime dependencies can be bundled into the Greengrass install dir

## 2024-03-07

- Fix conflicts with aws-device-sdk-cpp-v2 api handles
- Add dependency on openssl
- Plugins can be built as individual projects in linux
- Tests are now not built unless `-DBUILD_TESTING=1` is set
- Examples are not built unless `-DBUILD_EXAMPLES=1` is set
- aws-device-sdk-cpp-v2 is now shared between plugins

## 2024-02-22

- We have now added support for fetching thingName from environment variable.
- Example plugins are not installed by default now

### Known bug

- For support of TES you must also include the iot_broker plugin.
- The gglite binaries will only correctly run in a linux environment for now
- The local http server may not shut down properly and you might face the
  `mutex error` for that you need to find the process using the port. In linux
  environment you can use:

```
$ sudo ss -lptn 'sport = :8090'
$ kill <pid>
```

The first command tells you the process id as `("sh",pid=194410,fd=44))` and the
second one can kill the process.

## 2024-02-13

We have added support for TES LPC commands and the TES HTTP server. Components
started from local deployments which are using aws-device-sdk will be able to
use more AWS functionality such as S3 downloads.

Recipe parsing has been improved. Scripts are now passed to the shell which
parses and runs them. Recipes depending on shell features should now function.

Processes started for recipes are now managed by the gglite nucleus.

Recipe run keys are now case-insensitive.
