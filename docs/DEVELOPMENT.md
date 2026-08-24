# Greengrass nucleus lite Developer setup

This guide is for developers working on the Greengrass nucleus lite codebase to
set up.

## Using Nix

Using Nix will allow you to use a reproducible development environment matching
CI as well as run the CI checks locally.

Use the following Nix installer which enables used features automatically:
https://github.com/NixOS/nix-installer

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

## Releasing

The following is the process for cutting a release of this repo. Use previous
releases as examples.

1. Ensure SDK is pulled to lastest version or commit since then.
2. Ensure CI is green on main branch.
3. Make a release PR updating just the release notes and versions.
   1. Add a new section to top of release notes with sections with lists of new
      features and bug fixes customers should be made aware of. Skip mentioning
      commits which don't have customer impact.
   2. Update the version in the `version` file at the root of the repo.
   3. Use the format "Release vX.Y.Z" for the PR description and you can leave
      PR body blank.
4. Merge the release PR when ready.
5. Download the regular deb artifacts for x86_64, aarch64, and armv7 from the GH
   actions run of the release commit on main.
6. Tag the release with an annotated tag (`-a` flag to `git tag`).
   1. Run `git tag -a vX.Y.Z`
   2. For the tag message, use "vX.Y.Z release" for the title, and copy the new
      release notes section into the description. Remove the markdown markup
      from the copied release notes (lists are fine). Note that this should be
      79 col wrapped to match git conventions. See previous tags for examples.
   3. Push the tag after verifying it is an annotated tag.
7. Make a Github release from the tag. Use same title and body as the tag
   message. Note that Github does not handle line wrapping, so unwrap the lines.
   Add the deb artifacts to the release.
