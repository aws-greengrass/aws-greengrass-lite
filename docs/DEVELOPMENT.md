# Greengrass Lite Developer setup

This guide is for developers working on the Greengrass Lite codebase to set up.

## Using Nix

Using Nix will allow you to use a reproducible development environment matching
CI as well as run the CI checks locally.

Use the following Nix installer which enables used features automatically:
﻿https://github.com/DeterminateSystems/nix-installer

To run all the project formatters, run `nix fmt` in the project root directory.
Note that untracked git files will be formatted as well, so if using build
directories or other files not tracked by git or in gitignore, add them to your
`./.git/info/exclude`.

To reproduced the CI locally, run `nix flake check -L`.

If making a PR to main, you can check all of your branches commits with
`git rebase main -x "nix flake check -L"`.

## Using Podman
Docker does not fully support running systemd containters therefor we encourage to use podmen.
Those steps are necessary to compile inside a container.
```
sudo apt install podman

podman build misc/container -t ggl:latest

podman run -it -v $PWD/..:/work --replace --name ggl ggl:latest

cd /work/aws-greengrass-lite/

cmake -B build -DGGL_LOG_LEVEL=DEBUG

make -C build -j$(nproc)
```

## Creating deb aws-greengrass-lite deb package
After building, change into the build dir and run the cpack command will generate a deb package.
```
cd build/
cpack -G DEB
apt install ./aws-greengrass-lite-x.x.x-Linux.deb
```