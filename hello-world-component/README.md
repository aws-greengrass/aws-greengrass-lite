# com.example.LiteHelloWorld

A simple component that connects to Greengrass IPC and prints Hello World.

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
│   └── com.example.LiteHelloWorld
│       └── x.y.z
│           └── hello-world
└── recipes
    └── com.example.LiteHelloWorld-x.y.z.yaml
```

### Local Deploy

Run from your install dir, specifying the current version of
com.example.LiteHelloWorld in place of x.y.z

```
./bin/ggl-cli deploy --recipe-dir components/recipes --artifacts-dir components/artifacts --add-component com.example.LiteHelloWorld=x.y.z
```

Check the nucleus logs to verify that the deployment is SUCCEEDED.

### Check the component logs

After the deployment completes, read the logs from the component:

```
journalctl -f -u ggl.com.example.LiteHelloWorld.service
```
