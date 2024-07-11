# `recipe-runnerd` spec

`recipe-runnerd` will run a given scrpit file with ant of the provided
environment variables

- [recipe-runnerd-1] The executable will execute all the commands provided
  within the provided file
- [recipe-runnerd-2] The executable will also forward any provided environment
  variables to the running script
- [recipe-runnerd-3] On execution failure it prints the error message to stderr
- [recipe-runnerd-4] The executable will take only 1 file as an argument
- [recipe-runnerd-4] The Environment variable will be provide from unit files
- [recipe-runnerd-4] Additional Environment variable will be provide through
  args

## CLI parameters

### filePath

- [recipe-runnerd-param-filePath-1] The argument will provide the path to script
  file path.
- [recipe-runnerd-param-filePath-2] The filePath argument can be provided by
  `--filepath` or `-p`.
- [recipe-runnerd-param-filePath-3] The filePath argument is required.

### timeout

- [recipe-runnerd-param-timeout-1] The argument will allow user to edit the
  timeout seting for the given script in seconds.
- [recipe-runnerd-param-timeout-2] The deafult value for the parmeter is 30
  seconds.
- [recipe-runnerd-param-timeout-3] The timeout argument can be provided by
  `--timeout` or `-t`.
- [recipe-runnerd-param-timeout-4] The timeout argument is optional.

### environment

- [recipe-runnerd-param-environment-1] This argument will provide additional
  environment variable while executing the script
- [recipe-runnerd-param-environment-2] This argument will require to provide
  data in a json format
- [recipe-runnerd-param-environment-3] The environment argument can be provided
  by `--environment` or `-e`.
- [recipe-runnerd-param-environment-4] The environment argument is optional.

## Environment Variables

## Core Bus API
