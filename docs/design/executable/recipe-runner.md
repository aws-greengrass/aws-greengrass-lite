# `recipe-runner` design

See [`recipe-runnerd` spec](../../spec/executable/recipe-runner.md) for the
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

Once a unit file is created `ggdeploymentd` will use `recipe2unit`'s functions
to execute the specific unit file that will run provided lifecycle phase as a
first time installation process.

`recipe-runner` will use the provided selected lifecycle section and use
`execvpe` to execute the argument provided lifecycle section as a bash script.
It will also forward any environment variables set during runtime. As a side
effect it will create a temporary bash script file with all the gg-recipe
variables replaced with appropriate actual values from the global config and
then use will provide the newly created script file to `execvpe`.
