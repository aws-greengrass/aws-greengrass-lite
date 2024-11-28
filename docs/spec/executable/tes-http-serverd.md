# `tes-http-serverd` spec

`tes-http-serverd` implements a generic HTTP server which will host temporary
credentials for other processes to acquire.

For information on TES, see
<https://docs.aws.amazon.com/greengrass/v2/developerguide/interact-with-aws-services.html>.

The following is the current daemon implementation flow:

- [tes-http-serverd-1] The `tes-http-serverd` daemon will use the `tes` daemon
  for acquiring credentials.
- [tes-http-serverd-2] On connection, the `tes-http-serverd` daemon will
  validate the provided token and establish an http connection with the server.
- [tes-http-serverd-3] On request, the `tes-http-serverd` daemon will validate
  the request and headers.
- [tes-http-serverd-4] On request, the `tes-http-serverd` daemon will make a
  request to the `tes` daemon to acquire the temporary credentials vended, and
  return to the caller.

## CLI parameters

### port

- [tes-http-serverd-param-port-1] The port argument configures the port for
  which the server will run on. If not provided will default to port `8090`.

## Environment Variables

### AWS_CONTAINER_AUTHORIZATION_TOKEN

- Authorization token which is used to connected to the Token Exchange Service
  server hosted on `AWS_CONTAINER_CREDENTIALS_FULL_URI`. Greengrass Nucleus Lite
  will export this variable for example the AWS SDK to use.

### AWS_CONTAINER_CREDENTIALS_FULL_URI

- The URI in which the Token Exchange Service is hosted on. When a component
  creates an AWS SDK client, the client recognizes this URI environment variable
  and uses the token in the `AWS_CONTAINER_AUTHORIZATION_TOKEN` to connect to
  the token exchange service and retrieve AWS credentials. Greengrass Nucleus
  Lite will export this variable for example the AWS SDK to use.

## Core Bus API
