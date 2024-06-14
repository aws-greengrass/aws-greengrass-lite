# `gghttplib` spec

The GG-Lite HTTP library (`gghttplib`) is a library that implements HTTP related functions that other lite components may need.

## Key Use Cases

The following components are expected to have some reliance or functionality that is contained within the gghttplib:

* [usecase-1] Deployments daemon: During a typical deployments process, we expect to make several cloud calls to the Greengrassv2 Dataplane APIs which will require both GET and POST HTTP calls. These will need to be done over mutual authentication, providing the device certificate, private key, and root CA over mTLS. A subset of deployment features may be possible without the gghttplib.
* [usecase-2] TES daemon: The TES service needs to make a GET HTTP call to the IoT Credentials endpoint in order to retrieve the AWS credentials associated with the role alias. This will need to be done over mutual authentication, providing the device certificate, private key, and root CA over mTLS. TES will be unable to get the initial set of credentials or refresh credentials without the gghttplib.
* [usecase-3] Cloudwatch forwarder daemon: In order to forward logs to Cloudwatch Logs, POST HTTP requests to Cloudwatch Logs must be possible. These will need to authenticate with AWS credentials (from TES) and perform SigV4 signing on the call.

## Requirements

Requirements for the http library come from the outlined use cases above. These are kept generic for future extensibility but are the minimum requirement for supporting the above listed components.

Must-haves:

1. [gghttplib-1] The http library supports a function that can execute a generic HTTP GET call. (usecase 1, 2)
2. [gghttplib-2] The http library supports a function that can execute a generic HTTP POST call. (usecase 1, 3)
3. [gghttplib-3] The http library supports authentication via mTLS and can attach the necessary certificates for calls reaching IoT Core endpoints. (usecase 1, 2)
4. [gghttplib-4] The http library supports AWS SigV4 Signing for calls reaching AWS endpoints. (usecase 3)

Nice-to-haves:

1. [gghttplib-5] The http library can download response data directly to a file located at a specified path. (usecase 1, 2)

## Functions

The `gghttplib` should support the following functions in order to satisfy use case requirements:

### httpWithMTLS

The `httpWithMTLS` function makes a HTTP request to the specified endpoint and returns the response as a buffer. It uses the device certificate, private key, and root CA as specified in the Nucleus config to authenticate using mutual auth. If localPath is specified, it does not return the response as a buffer but rather a confirmation that the file writing is complete.

* `url` is a required parameter of type buffer
    * `url` must contain the full url endpoint for the http call.
* `action` is a required parameter of type buffer
    * `action` must be one of `GET` or `POST`.
* `body` is an optional parameter of type buffer
    * `body` is the body of the HTTP call.
    * This parameter should only be provided for a `POST` request. If action is specified as `GET`, then this parameter is ignored.
* `localPath` is an optional parameter of type buffer
    * `localPath` is a path that the response of the HTTP call should be downloaded to.
    * If `localPath` is not provided, the response is only returned in-memory. If it is provided, the in-memory response does not include the http response.

### httpWithSigV4

The `httpWithSigV4` function makes a HTTP request to the specified endpoint and returns the response as a buffer. It will sign the HTTP call according to the AWS SigV4 algorithm.

* `url` is a required parameter of type buffer
    * `url` must contain the full url endpoint for the http call.
* `action` is a required parameter of type buffer
    * `action` must be one of `GET` or `POST`.
* `body` is an optional parameter of type buffer
    * `body` is the body of the HTTP call.
    * This parameter should only be provided for a `POST` request. If action is specified as `GET`, then this parameter is ignored.
* `credentials` is a required parameter of type map
    * The `credentials` map should include the following keys and associated values as buffers:
        * `AccessKeyId`: The AWS credentials access key
        * `SecretAccessKey`: The AWS credentials secret access key
        * `Token`: The AWS credentials token
        * `Expiration`: The expiration for the AWS credentials
