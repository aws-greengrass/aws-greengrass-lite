# AWS Greengrass nucleus lite

AWS IoT Greengrass runtime for constrained devices.

The Greengrass nucleus lite provides a lightweight alternative to the Greengrass
nucleus runtime.

The nucleus lite aims to be compatible with the Greengrass nucleus, but
implements a subset of its functionality. Expect future releases to reduce the
feature gap.

## Getting started

See the [build guide](docs/BUILD.md) for instructions to build and install
Greengrass Nucleus Lite from source.

To configure and run Greengrass Nucleus Lite, see the
[setup guide](docs/SETUP.md).

For setting up as a Greengrass developer, also see the
[developer setup guide](docs/DEVELOPMENT.md).

For AI agent driven getting started please follow the instruction from the
[greengrass-agent-context-pack github repo](https://github.com/aws-greengrass/greengrass-agent-context-pack).

For easy device onboarding and example implementation, you may want to check out
[Avnet's workshop](https://event.on24.com/wcc/r/5114804/16BB67D34A48F65741B4C0A5EA675F1A).

Furthermore you can visit Avnet's IOTCONNECT Greengrass repository
[on GitHub](https://github.com/avnet-iotconnect/iotc-python-greengrass-sdk) for
SDKs and Quick Start guides that support platforms such as STM32, Renesas, NXP
and Raspberry Pi.

### ⚠️ Important Notice

The git tags in this repository represent stable, fully tested releases. Please
use these for production environments.

The `main` branch contains ongoing development work and:

- May contain untested features.
- Could include breaking changes.
- Is not recommended for production use.

### ⚠️ RISC-V Support Warning

RISC-V architecture support is experimental and not fully tested. Use with
caution in production environments.

## Supported Greengrass V2 IPC commands (Features)

IPC support is provided by ggipcd. The support translates the IPC command to
corebus. This table identifies the corebus component that does the work.

| Feature                        | Daemon that provides support |
| :----------------------------- | :--------------------------- |
| SubscribeToTopic               | ggpubsubd                    |
| PublishToTopic                 | ggpubsubd                    |
| PublishToIoTCore               | iotcored                     |
| SubscribeToIoTCore             | iotcored                     |
| GetConfiguration               | ggconfigd                    |
| UpdateConfiguration            | ggconfigd                    |
| SubscribeToConfigurationUpdate | ggconfigd                    |
| CreateLocalDeployment          | ggdeploymentd                |
| ValidateAuthorizationToken     | ggipcd                       |
| RestartComponent               | gghealthd                    |
| UpdateState                    | gghealthd                    |

Additional IPC commands will be supported in future releases.

## Additional Details

Known issues are documented
[here](https://github.com/aws-greengrass/aws-greengrass-lite/issues) with some
potential workarounds. Additionally, only basic recipe types are supported, more
information on missing features can be found
[here](./docs/RECIPE_SUPPORT_CHANGES.md).

## Security

See [CONTRIBUTING](docs/CONTRIBUTING.md#security-issue-notifications) for more
information.

## License

This project is licensed under the Apache-2.0 License.
