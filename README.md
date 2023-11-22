## Greengrass Lite

A smaller version of the Greengrass Nucleus for resource-constrained devices.

Greengrass Lite provides compatibility with Greengrass v2 components.
Additionally, it provides a new plugin interface; functionality can be built as
a shared library that uses the plugin interface to extend nucleus functionality.

See <https://github.com/aws-greengrass/aws-greengrass-nucleus/> for more.

The `nucleus` directory has the Greengrass Lite nucleus, which is the core of
the system.

The `plugins` directory has plugins, some of which implement optional Greengrass
Lite functionality, and others serve as examples of third-party plugins.

The `plugin_api` directory has the interface plugins should build against.

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more
information.

## License

This project is licensed under the Apache-2.0 License.
