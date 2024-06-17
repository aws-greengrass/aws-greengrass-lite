# `gghttplib` spec

The GG-Lite HTTP library (`gghttplib`) is a library that implements HTTP related
functions that other lite components may need.

The following components are expected to have some reliance or functionality
that is contained within the gghttplib:

- Deployments daemon: During a typical deployments process, we
  expect to make several cloud calls to the Greengrassv2 Dataplane APIs which
  will require both GET and POST HTTP calls. These will need to be done over
  mutual authentication, providing the device certificate, private key, and root
  CA over mTLS. A subset of deployment features may be possible without the
  gghttplib.
- TES daemon: The TES service needs to make a GET HTTP call to the
  IoT Credentials endpoint in order to retrieve the AWS credentials associated
  with the role alias. This will need to be done over mutual authentication,
  providing the device certificate, private key, and root CA over mTLS. TES will
  be unable to get the initial set of credentials or refresh credentials without
  the gghttplib.
- Cloudwatch forwarder daemon: In order to forward logs to
  Cloudwatch Logs, POST HTTP requests to Cloudwatch Logs must be possible. These
  will need to authenticate with AWS credentials (from TES) and perform SigV4
  signing on the call.

## Requirements


1. [gghttplib-1] The http library supports a function that can execute a generic
   HTTP GET call. (usecase 1, 2)
2. [gghttplib-2] The http library supports a function that can execute a generic
   HTTP POST call. (usecase 1, 3)
3. [gghttplib-3] The http library supports authentication via mTLS and can
   attach the necessary certificates for calls reaching IoT Core endpoints.
   (usecase 1, 2)
4. [gghttplib-4] The http library supports AWS SigV4 Signing for calls reaching
   AWS endpoints. (usecase 3)

## Functions

The `gghttplib` should support the following functions in order to satisfy use
case requirements:

### http_with_mtls

The `httpWithMTLS` function makes a HTTP request to the specified endpoint and
returns the response as a buffer. It uses the device certificate, private key,
and root CA retrieved from the config module to authenticate using mutual
auth. If localPath is specified, it does not return the response as a buffer but
rather a confirmation that the file writing is complete.

- [gghttplib-httpWithMTLS-params-1] `url` is a required parameter of type buffer
  - [gghttplib-httpWithMTLS-params-1.1] `url` must contain the full url endpoint
    for the http call.
- [gghttplib-httpWithMTLS-params-2] `action` is a required parameter of type
  buffer
  - [gghttplib-httpWithMTLS-params-2.1] `action` must be one of `GET` or `POST`.
- [gghttplib-httpWithMTLS-params-3] `body` is an optional parameter of type
  buffer
  - [gghttplib-httpWithMTLS-params-3.1] `body` is the body of the HTTP call.
  - [gghttplib-httpWithMTLS-params-3.2] This parameter should only be provided
    for a `POST` request. If action is specified as `GET`, then this parameter
    is ignored.
- [gghttplib-httpWithMTLS-params-4] `localPath` is an optional parameter of type
  buffer
  - [gghttplib-httpWithMTLS-params-4.1] `localPath` is a path that the response
    of the HTTP call should be downloaded to.
  - [gghttplib-httpWithMTLS-params-4.2] If `localPath` is not provided, the
    response is only returned in-memory. If it is provided, the in-memory
    response does not include the http response.

### http_with_sigv4

The `httpWithSigV4` function makes a HTTP request to the specified endpoint and
returns the response as a buffer. It will sign the HTTP call according to the
AWS SigV4 algorithm.

- [gghttplib-httpWithSigV4-params-1] `url` is a required parameter of type
  buffer
  - [gghttplib-httpWithSigV4-params-1.1] `url` must contain the full url
    endpoint for the http call.
- [gghttplib-httpWithSigV4-params-2] `action` is a required parameter of type
  buffer
  - [gghttplib-httpWithSigV4-params-2.1] `action` must be one of `GET` or
    `POST`.
- [gghttplib-httpWithSigV4-params-3] `body` is an optional parameter of type
  buffer
  - [gghttplib-httpWithSigV4-params-3.1] `body` is the body of the HTTP call.
  - [gghttplib-httpWithSigV4-params-3.2] This parameter should only be provided
    for a `POST` request. If action is specified as `GET`, then this parameter
    is ignored.
- [gghttplib-httpWithSigV4-params-4] `credentials` is a required parameter of
  type map
  - [gghttplib-httpWithSigV4-params-4.1] The `credentials` map should include
    the following keys and associated values as buffers:
    - [gghttplib-httpWithSigV4-params-4.1.1] `access_key_id`: The AWS credentials
      access key
    - [gghttplib-httpWithSigV4-params-4.1.2] `SecretAccessKey`: The AWS
      credentials secret access key
    - [gghttplib-httpWithSigV4-params-4.1.3] `Token`: The AWS credentials token
    - [gghttplib-httpWithSigV4-params-4.1.4] `Expiration`: The expiration for
      the AWS credentials
