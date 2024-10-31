# `recipe-runner` design

See [`recipe-runner` spec](../../spec/executable/recipe-runner.md) for the
public interface for `recipe-runnerd`.

`recipe-runner` is designed to run as a wrapper on the recipe's lifecycle script
section. It will take the script section as is and will replace the recipe
variables with appropriate values during runtime. The executable also
understands the gg-global config and how to interact with it.

Once a recipe is translated to a unit file, the selected lifecycle will be
converted to a json file with its different lifecycle section. Each lifecycle
section will generate a unit file that is suffixed with it's phase. For an
example a recipe names `sampleComponent-0.1.0` will have unit files named
`sampleComponent-0.1.0_install` and `sampleComponent-0.1.0_run` to represent
install and run phase of lifecycle. As per the recipe2unit's design.

The recipe translation to a unit file relies upon `recipe-runner` to execute
the scripts in the lifecycle sections.

Example use from a systemd unit file
```C
ExecStart=/opt/aws-greengrass-lite/bin/recipe-runner -n myGenericComponent -p /var/aws-greengrass-lite/launch_scriptmyGenericComponentinstall
```

`recipe-runner` will use`execvpe` to execute the argument provided bash script.
It will also forward any environment variables set during runtime.
