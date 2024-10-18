# aws.greengrass.CloudLogger

A generic component which uploads system logs to cloudwatch

### Build

This section is subject to change.

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

### Local Deploy

This section is subject to change.

Run from your install dir, specifying the current version of CloudLogger in
place of x.y.z

```
./bin/ggl-cli deploy --recipe-dir components/recipes --artifacts-dir components/artifacts --add-component aws.greengrass.CloudLogger=x.y.z
```

//TODO: Add cloudformation template and/or setup instructions for the log group,
rule action, role, etc
