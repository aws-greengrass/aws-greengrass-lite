# Release Notes v2.0.0 (Dec 16 2024)

This the first release for Greengrass nucleus lite product line. Greengrass
nucleus lite v2 aims to be compatable with AWS's GreenGrass v2(GGV2) product
line, however currently only a subset of the features are supported with this
release.

## Installing from sources

To install nucleus lite from source, please follow the guide from
[SETUP.md](./docs/SETUP.md) and [TES.md](./docs/TES.md), once the setup
environment is complete, please start refering to
[INSTALL.md](./docs/INSTALL.md).

For those intrested in a generic method of provisioning nucleus lite device,
fleet provisioning by claim may be one of the appropriate solution to the
problem. Please refer to fleet provisioning setup guide
[here](./docs/Fleet-provisioning.md).

## For Developers

Thank you for your interest in contributing to nucleus lite project. Whether
it's a bug report, new feature, code, or documentation, we welcome our community
to be involved in this project. Guidelines on contributing to repo can be found
[here](./docs/CONTRIBUTING.md)

It's highly recommended to setup a nix environment, more information on setup
can be found [here](./docs/DEVELOPMENT.md).

## New with this release

The desing document for the Greengrass nucleus lite can be found in the
[design](./docs/design/) and [spec](./docs/spec/) directories.

Greengrass nucleus lite is currently not fully compatible with GGV2. Greengrass
nucleus lite only has basic feature support. The follow up releases will add
more feature to reach feature parity with GGV2 in near future. The basic recipe
types that are presently supported along with detailed information can be found
[here](./docs/RECIPE_SUPPORT_CHANGES.md).

## Known issues

The differences between Greengrass nucleus lite and GGV2 along with the
currently known issues can be found in the
[KNOWN_ISSUES](./docs/KNOWN_ISSUES.md) document and Github issues.

We welcome you to create a Github issue to report bugs or suggest features.
