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

For easy device onboarding and examples for specific boards, visit Avnet’s
/IOTCONNECT Greengrass repository
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

## Supported Greengrass V2 IPC commands (Features)

IPC support is provided by ggipcd. The support is translating the IPC command to
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

Additional IPC commands will be supported in future releases.

## Additional Details

Known issues are documented
[here](https://github.com/aws-greengrass/aws-greengrass-lite/issues) with some
potential workarounds. Additionally only basic recipe types are supported, more
information on missing features can be found
[here](./docs/RECIPE_SUPPORT_CHANGES.md).

## Security

See [CONTRIBUTING](docs/CONTRIBUTING.md#security-issue-notifications) for more
information.

## License

This project is licensed under the Apache-2.0 License.
