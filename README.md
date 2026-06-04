# AWS Greengrass nucleus lite

AWS IoT Greengrass runtime for constrained devices.

The Greengrass nucleus lite provides a lightweight alternative to the Greengrass
nucleus runtime.

The nucleus lite aims to be compatible with the Greengrass nucleus, but
implements a subset of its functionality. Expect future releases to reduce the
feature gap.

## Getting started

See the [build guide](docs/BUILD.md) for instructions to build and install
Greengrass nucleus lite from source.

To configure and run Greengrass nucleus lite, see the
[setup guide](docs/SETUP.md).

For setting up as a Greengrass developer, also see the
[developer setup guide](docs/DEVELOPMENT.md).

For recommended device specs and measured RAM / disk / CPU footprint across
x86_64, aarch64, and armv7l, see the
[resource limits documentation](docs/RESOURCE_LIMITS.md).

For AI agent driven getting started please follow the instruction from the
[greengrass-agent-context-pack github repo](https://github.com/aws-greengrass/greengrass-agent-context-pack).

For accelerated development, easy device onboarding, and sample components,
check out Avnet's
[hands-on workshops](https://event.on24.com/wcc/mr/6724766/6E4BE6E2CF7D23EF4BA022C2F8783E80)
covering Device Health Monitoring and Vision AI Object Detection.

Furthermore you can visit Avnet's /IOTCONNECT Greengrass repository
[on GitHub](https://github.com/avnet-iotconnect/iotc-python-greengrass-sdk) for
SDKs and Quick Start guides that support platforms such as STM32, Renesas, NXP
and Raspberry Pi.

For a self-paced workshop, see the
[Greengrass Nucleus Lite Workshop](https://catalog.workshops.aws/greengrass-nucleus-lite/en-US).

For Yocto/OpenEmbedded integration, check out
[meta-aws](https://github.com/aws4embeddedlinux/meta-aws) and
[meta-aws-demos](https://github.com/aws4embeddedlinux/meta-aws-demos) which
provide recipes and examples for building AWS Greengrass Lite.

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
