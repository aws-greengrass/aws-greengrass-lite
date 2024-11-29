# Greengrass Nucleus Lite Developer setup

This guide is for developers working on the Greengrass Nucleus Lite codebase to
set up.

## Using Nix

Using Nix will allow you to use a reproducible development environment matching
CI as well as run the CI checks locally.

Use the following Nix installer which enables used features automatically:
﻿https://github.com/DeterminateSystems/nix-installer

If using a Nix environment for building, you will need to pass
`CMAKE_INSTALL_PREFIX` when running CMake.

To run all the project formatters, run `nix fmt` in the project root directory.
Note that untracked git files will be formatted as well, so if using build
directories or other files not tracked by git or in gitignore, add them to your
`./.git/info/exclude`.

To reproduced the CI locally, run `nix flake check -L`.

If making a PR to main, you can check all of your branches commits with
`git rebase main -x "nix flake check -L"`.

## Running Coverity

After installing Coverity and adding its bin dir to your path, run the following
in the project root dir:

```sh
cmake -B build
coverity scan
```

The html output will be in `build/cov-out`.

## Creating deb aws-greengrass-lite deb package

After building, change into the build dir and run the cpack command will
generate a deb package.

```sh
cd build/
cpack -G DEB
apt install ./aws-greengrass-lite-x.x.x-Linux.deb
```

This can also be done in the buildtestcontainer

```
podman build misc/buildtestcontainer -t buildtestcontainer:latest
podman run -it -v $PWD/..:/work --replace --name buildtestcontainer buildtestcontainer:latest
```
