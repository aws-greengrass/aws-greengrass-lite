# `teshttpserverd` spec

`teshttpserverd` implements a generic HTTP server which will host temporary
credentials for other processes to acquire.

For information on TES, see
<https://docs.aws.amazon.com/greengrass/v2/developerguide/token-exchange-service-component.html>.

The following is the current daemon implementation flow:

- [teshttpserverd-1] The `teshttpserverd` daemon will use the `tes` daemon for
  acquiring credentials.
- [teshttpserverd-2] On connection, the `teshttpserverd` daemon will validate
  the provided token and establish an http connection with the server.
- [teshttpserverd-3] On request, the `teshttpserverd` daemon will validate the
  request and headers.
- [teshttpserverd-4] On request, the `teshttpserverd` daemon will make a request
  to the `tes` daemon to acquire the temporary credentials vended, and return to
  the caller.

## CLI parameters

### port

- [teshttpserverd-param-port-1] The port argument configures the port for which
  the server will run on. If not provided will default to port `8090`.

#### Error Handling

- [teshttpserverd-cli-error-1] `Invalid port provided`
  - The port provided was not valid, make sure the port is in the valid range of
    `1024–49151`.
- [teshttpserverd-cli-error-2]
  `Unable to start the TES HTTP Server daemon on configured port`
  - The TES HTTP Server daemon was unable to start on provided port. Make sure
    the `teshttpserverd` daemon is configured properly with a port that is not
    in use.

## Environment Variables

### AWS_CONTAINER_AUTHORIZATION_TOKEN

- Authorization token which is used to connected to the Token Exchange Service
  server hosted on `AWS_CONTAINER_CREDENTIALS_FULL_URI`. Greengrass lite will
  export this variable for example the AWS SDK to use.

### AWS_CONTAINER_CREDENTIALS_FULL_URI

- The URI in which the Token Exchange Service is hosted on. When a component
  creates an AWS SDK client, the client recognizes this URI environment variable
  and uses the token in the `AWS_CONTAINER_AUTHORIZATION_TOKEN` to connect to
  the token exchange service and retrieve AWS credentials. Greengrass lite will
  export this variable for example the AWS SDK to use.

## Core Bus API
