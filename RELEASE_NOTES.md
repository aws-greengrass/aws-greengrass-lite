# Release Notes v2.3.3

This release fixes some bugs and has minor improvements:

- Fixes leak of fds when creating components during deployment
- Fixes issue where deployment job status report may be rejected, resulting in
  current deployment status not being reported
- Allows component install phases to retry up to 3 times before failing
  deployment instead of after first fail
- Increases socket timeouts to be more generous

# Release Notes v2.3.0

This release includes support for using TPM 2.0 for IoT Core MQTT authorization
and the `RestartComponent` IPC command.

## Breaking Changes

`GetConfiguration` has been updated to match the Greengrass nucleus runtime
behavior. With this release, the nucleus lite runtime will return the same
results as nucleus when used with the AWS IoT Device SDKs. Users of
aws-greengrass-sdk-lite will need to update to version 0.3.0 of the SDK.

Recipe manifests must now have the platform runtime set to `aws_nucleus_lite` or
`*`. A missing platform runtime is now correctly handled as classic only.

## New with this release

- Local deployments no longer require internet access.
- The minimum TLS protocol version is now set to 1.2.
- TPM 2.0 persistent handles in the `privateKeyPath` config are supported. For
  instructions, see the documentation here:
  https://github.com/aws-greengrass/aws-greengrass-lite/blob/main/docs/TPM_SUPPORT.md
- Updated the sample fleet provisioning template.
- The sample APT packages now support more operating systems: Ubuntu 22.04,
  Ubuntu 24.04, Debian 12, and Debian 13.
- Moved the fleet provisioning credentials storage path to
  `/var/lib/greengrass/credentials`.

# Release Notes v2.2.2

This release fixes the folowing bugs:

- Fixes revised deployments containing unchanged component versions failing when
  an unchanged component had a running executable as an artifact.
- Fixes recipe variable interpolation when the interpolated value has more than
  four nested subobjects.
- Fixes recipe variable interpolation including quotes and shell special
  characters.
- Fix leak of fds when MQTT connections fail.

# Release Notes v2.2.1

This release fixes a regression from v2.2.0 where the nucleus fails to obtain
TES credentials.

# Release Notes v2.2.0

This release includes support for pulling images from container registries,
including Docker and public/private ECR.

## New with this release

- Added support for container image artifact URIs. Prepend `docker:` to valid
  image names to declare them as artifacts (i.e.
  `docker:registry/image[:tag|@digest]`). Missing images are pulled when
  deploying a component with Docker artifacts.
- IPC access control policies now supports the `"*"` wildcard for policy
  operations.
- Fixed failure when aws-device-sdk-python-v2 calls PublishToIoTCore with a QoS.
- Fixed bug with vending TES credentials to components introduced in v2.1.0.

Docker must be installed and configured in order to pull registry images and run
containers on a Greengrass Lite Core Device. For instructions, view
documentation here:
https://docs.aws.amazon.com/greengrass/v2/developerguide/run-docker-container.html

# Release Notes v2.1.0

This release includes HTTP proxy support for the AWS Greengrass Nucleus Lite
runtime.

## New with this release

- Added HTTP proxy support that can be configured using the networkProxy
  configuration option
- Lowered the requirement of `libcurl` from 7.86 to 7.82 for devices running
  older versions
- Updates `journalctl` logs so they are attributed to components instead of
  recipe-runner
- Improved error responses for IPC calls
- Added retries for S3 download attempts (for generic component artifacts)
- Minor bug fixes

# Release Notes v2.0.2

This is a minor release that fixes the dependencies of the apt packages to
include cgroup-tools.

# Release Notes v2.0.1

This is a minor release that adds the missing features to support default recipe
of Stream manager v2.2.0.

## New with this release

- Add recipe variable interpolation to greengrass recipe's timeout section
- Add support for ValidateAuthorizationToken IPC command for stream manager
- Fix warnings from Fleet provisioning
- Add retry and backoff to jobs listener

# Release Notes v2.0.0 (Dec 16 2024)

This is the first release of the Greengrass nucleus lite. It aims to be
compatible with the AWS IoT GreenGrass API and the previous Greengrass nucleus
implementations, however currently only a subset of the features are supported
with this release. Expect future releases to reduce the feature gap with
Greengrass nucleus.

In particular, only basic component recipe types are presently supported.
Detailed information can be found [here](./docs/RECIPE_SUPPORT_CHANGES.md).

## Installing from source

To install Greengrass nucleus lite from source, please follow the build guide
[BUILD.md](./docs/BUILD.md) and [Provisioning.md](./docs/Provisioning.md). Once
the development environment is setup, please refer to
[SETUP.md](./docs/SETUP.md).

## New with this release

This is the first release of Greengrass nucleus lite.

## Known issues

For an updated list of issues/feature requests, please take a look at GitHub
issues in the repository.

We welcome you to create an issue to report bugs or suggest features.
