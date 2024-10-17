# aws.greengrass.CloudLogger

A generic component which uploads system logs to cloudwatch

### Build

This section is subject to change.

See [the build+install guide for GG-lite](../docs/INSTALL.md). When installing
GG-lite, you will get a `components` directory in your install dir. `components`
contains the artifacts and recipes directory structure that can be used by local
deployments:

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

Run from your install dir, specifying the appropriate version of CloudLogger in
place of x.y.z

```
./bin/ggl-cli deploy --recipe-dir components/recipes --artifacts-dir components/artifacts --add-component aws.greengrass.CloudLogger=x.y.z
```
