# `tes-http-serverd` spec

`tes-http-serverd` implements a generic HTTP server which will host temporary
credentials for other processes to acquire.

For information on TES, see
<https://docs.aws.amazon.com/greengrass/v2/developerguide/interact-with-aws-services.html>.

The following are the requirements for the `tes-http-serverd`:

- [tes-http-serverd-1] The `tes-http-serverd` daemon will use the `tes` daemon
  for acquiring TES credentials.
- [tes-http-serverd-2] On request, the `tes-http-serverd` daemon will validate
  the request and headers.
- [tes-http-serverd-3] On request, the `tes-http-serverd` daemon will make a
  request to the `tes` daemon to acquire the temporary credentials vended, and
  return to the caller.
- [tes-http-serverd-4] The `tes-http-serverd` will accept the argument --version
  (-v) to display the version number of the serverd executable.
- [tes-http-serverd-5] The `tes-http-serverd` will accept the argument --help
  (-h) to display all the supported arguments and their use.
- [tes-http-serverd-6] The server socket port shall be OS selected at random
  from a free port.
- [tes-http-serverd-7] After the server socket is created & bound, the socket
  port must be written to the config key
  `services/aws.greengrass.TokenExchangeService/configuration/port`.
- [tes-http-serverd-8] The TES request must be authenticated with an
  authorization token provided by the client.
- [tes-http-serverd-9] The authorization token must be validated by the
  `ipc_component` coreBus responder with the `verify_svcuid` corebus message
- [tes-http-serverd-10] The authorization token must be 16-octets long.
- [tes-http-serverd-11] The `tes-http-serverd` shall not cache or otherwise
  store any credentials. All credentials must be obtained fresh via corebus
  transactions with the `tesd` process.

### Notes

> 1: Requirement `tes-http-serverd-11` above is to encourage the TES HTTP server
> to be as small as possible. Any caching should be done in a central place to
> the system and with c-groups it may be necessary to start multiple
> tes-http-serverd's

> 2: The authentication token is provided to the client component by the
> environment variable `AWS_CONTAINER_AUTHORIZATION_TOKEN`. The client must send
> this token to the tes-http-serverd in the TES request.

> 3: The tes-http-serverd URI shall be provided to all generic components as the
> environment variable `AWS_CONTAINER_CREDENTIALS_FULL_URI`.
