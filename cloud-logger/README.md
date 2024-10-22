# aws.greengrass.CloudLogger

A generic component which uploads system logs to CloudWatch.

This works by publishing system log messages on the IoT Core MQTT topic
`gglite/<thing-name>/logs`, which will be configured to forward the logs to
CloudWatch via an IoT Rule.

### Build

If you're building and installing GG-Lite, this component builds to a
locally-deployable state as part of that process. See
[the build+install guide for GG-Lite](../docs/INSTALL.md).

When installing GG-lite, you will get a `components` directory in your install
dir. `components` contains the artifacts and recipes directory structure that
can be used by local deployments:

```
components
├── artifacts
│   └── aws.greengrass.CloudLogger
│       └── x.y.z
│           └── cloud-logger
└── recipes
    └── aws.greengrass.CloudLogger-x.y.z.yaml
```

### Prerequisites

Before deploying this component, you should set up the cloud infrastructure to
receive the output from this component.

1. Follow the linked instructions to
   [create a CloudWatch log group.](https://docs.aws.amazon.com/iot/latest/developerguide/uploading-logs-rules-action-procedure.html#uploading-logs-rules-setup-log-group)
2. Follow the linked instructions to
   [create a topic rule](https://docs.aws.amazon.com/iot/latest/developerguide/uploading-logs-rules-action-procedure.html#uploading-logs-rules-setup-topic-rule),
   but with the following important notes:
   - This component outputs to the MQTT topic `gglite/<thing-name>/logs`, so
     when entering your SQL statement, use e.g. `gglite/ExampleGGDevice/logs`
     instead of `$aws/rules/things/thing_name/logs`.
   - When entering the rule action, **do not** enable batch mode.

### Local Deploy

Run from your install dir, specifying the current version of CloudLogger in
place of x.y.z

```
./bin/ggl-cli deploy --recipe-dir components/recipes --artifacts-dir components/artifacts --add-component aws.greengrass.CloudLogger=x.y.z
```
