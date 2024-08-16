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
