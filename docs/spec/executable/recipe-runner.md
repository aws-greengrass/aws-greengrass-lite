# `recipe-runnerd` spec

`recipe-runnerd` will act like a wrapper around the generic component to
dynamically update the GG-recipe variables

- [recipe-runnerd-1] The executable will execute all the commands within a selected phase as a bash script.
- [recipe-runnerd-2] The executable will also forward its environment variables
  to the running script using global config.
- [recipe-runnerd-3] On execution failure it prints the error message to stderr
- [recipe-runnerd-4] The executable will take only 1 file as an argument and phase

## CLI parameters

## phase

- [recipe-runnerd-param-phase-1] The argument will dectate which phase needs to
  be executed.
- [recipe-runnerd-param-phase-2] The phase argument can be provided by `--phase`
  or `-p`.
- [recipe-runnerd-param-phase-3] The phase argument is required.

### filePath

- [recipe-runnerd-param-filePath-1] The argument will provide the path to selected lifecycle json.
- [recipe-runnerd-param-filePath-2] The filePath argument can be provided by
  `--filepath` or `-f`.
- [recipe-runnerd-param-filePath-3] The filePath argument is required.

### timeout

- [recipe-runnerd-param-timeout-1] The argument will allow user to edit the
  timeout seting for the given script in seconds.
- [recipe-runnerd-param-timeout-2] The deafult value for the parmeter is 30
  seconds.
- [recipe-runnerd-param-timeout-3] The timeout argument can be provided by
  `--timeout` or `-t`.
- [recipe-runnerd-param-timeout-4] The timeout argument is optional.

## Environment Variables
