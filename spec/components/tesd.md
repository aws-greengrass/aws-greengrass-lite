# `tesd` spec

`tesd` implements a proxy service called the Token Exchange Service (TES) which
vends temporary credentials for other processes.

For information on TES, see
<https://docs.aws.amazon.com/greengrass/v2/developerguide/interact-with-aws-services.html>.

The following is the current daemon implementation flow:

- [tesd-1] The TES daemon will create a process connection to the downloader
  lib.
- [tesd-2] The TES daemon will use the device certificate, root CA, and private
  key, to make an HTTP request using the downloader lib to their IoT credential
  endpoint for a session token and access key.
- [tesd-3] Other processes can now make requests on the core bus for the
  temporary credentials, such as the `teshttpserver` daemon which will host the
  credentials on a port for the AWS SDK to acquire.

## CLI parameters

### endpoint

- [tesd-param-endpoint-1] The endpoint argument configures the credential
  endpoint the daemon connects to.
- [tesd-param-endpoint-2] The endpoint argument can be provided by `--endpoint`
  or `-e`.
- [tesd-param-endpoint-3] The endpoint argument is required.

### rootca

- [tesd-param-rootca-1] The rootca argument specifies the path to the root CA
  certificate file used to validate the AWS IoT Core endpoint, in PEM format.
  Ensure that it has the trailing newline character, as some SSL libraries do
  not support PEM files with missing trailing newline.
- [tesd-param-rootca-2] The rootca argument can be provided by `--rootca` or
  `-r`.
- [tesd-param-rootca-3] The rootca argument is required.

### cert

- [tesd-param-cert-1] The cert argument specifies the path to the client
  certificate PEM file to be used for TLS mutual auth with AWS IoT Core to
  authenticate the device.
- [tesd-param-cert-2] The cert argument can be provided by `--cert` or `-c`.
- [tesd-param-cert-3] The cert argument is required.

### key

- [tesd-param-key-1] The key argument specifies the path to the private key PEM
  file corresponding to the `cert` argument.
- [tesd-param-key-2] The key argument can be provided by `--key` or `-k`.
- [tesd-param-key-3] The key argument is required.

## Core Bus API

Each of the APIs below take a single map as the argument to the call, with the
key-value pairs described by the parameters listed in their respective sections.

### request_credentials

The request method will return back to the caller temporary IoT Core session
credentials.

- [tesd-bus-request_credentials-1] `token` is an optional parameter of type
  buffer.
  - [tesd-bus-request_credentials-1.1] `token` can contain an unique token
    identifier which will be validated before retrieval.
- [iotcored-bus-request_credentials-2] The method response is a map containing
  `accesskeyid`, `secretaccesskey`, `token`, and `expiration` keys, all of which
  have values of type buffer.
