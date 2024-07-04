# `recipe2unit` spec

`recipe2unit` converts a GG recipe into a linux specific systemd unit file so
that it can be deployed within the edge device.

- [recipe2unit-1] The executable intakes a GG recipe file and spits out a .unit
  file with all the features represented within the recipe.
- [recipe2unit-2] In case of an error it will output `Error Parsing Recipe`
  along with appropriate error message.

## CLI parameters

### recipe-path

- [recipe2unit-param-path-1] This argument will allow user to specify the
  recipe's location within the disk.
- [recipe2unit-param-path-2] The argument must be provided by `--recipe-path`.
- [recipe2unit-param-path-3] The argument is a required field

## Environment Variables

## Core Bus API
