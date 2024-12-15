# Release Notes v2.0.0 (Dec 16 2024)

This the first release for Greengrass nucleus lite product line. Greengrass
nucleus lite v2 aims to be compatable with AWS's GreenGrass v2(GGV2) product
line, however currently only subset of the features are supported with this
release.

## Installing from sources

To install nucleus lite from source, please follow the guide from
[SETUP.md](./docs/SETUP.md) and [TES.md](./docs/TES.md), once the setup
environment is complete, please start refering to
[INSTALL.md](./docs/INSTALL.md) file.

> For developers planning to contribute to nucleus lite, it's highly recommended
> to setup a nix environment, more information on setup can be found
> [here](./docs/DEVELOPMENT.md).

For those intrested in a generic method of provisioning nucleus lite device,
fleet provisioning by claim may be one of the appropriate solution to the
problem. Please refer to fleet provisioning setup guide
[here](./docs/Fleet-provisioning.md).

## New with this release

To provide more transparency on decisions and be in sync with the community all
the design docs and specs for the project are now provided with this release.
You can find more details on the [design doc](./docs/design/) and
[spec doc](./docs/spec/) by following the corresponding links.

As mentioned at the start, currently nucleus lite isn't fully compatable with
GGV2. The plan is to release this initail version of nucleus lite with basic
feature support first and then gain full feature compatibility with future
releases. Currently only basic recipe types are supported and inforation on the
supported recipe can be found here [here](./docs/RECIPE_SUPPORT_CHANGES.md).

Furthermore there are known difference in behavior that are outlined by
[KNOWN_ISSUES](./docs/KNOWN_ISSUES.md) document. Also please look at the Github
issues section to keep up with latest known issues. It's encouraged to open up a
Github issues for any behaviors that is noticed by the contributors but not
coverted by the doc or Github issues.
