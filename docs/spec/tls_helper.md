# Greengrass Lite TLS helper API

AWS Greengrass Lite will allow TLS communication support to be pluggable through
the TLS helper interface. This enables allow customers to plug in custom TLS
setups, such as using TPM, PKCS#11, a different TLS library, etc.

## Requirements

The TLS interface is designed to meet the following requirements:

- Allow TLS support to be pluggable with custom implementations.
- Enable writing an implementation without a library dependency.
- Keep TLS libraries out of the main daemon process (potentially reduces memory
  usage).
- Allow efficient transport using kTLS.
- Allow for more complex uses cases such as nested TLS though a HTTPS proxy.

## Interface

The TLS helper must be on the PATH for the Greengrass nucleus daemons. The
binary name must be `ggl-tls-helper`. Greengrass nucleus daemons will invoke it
by executing `ggl-tls-helper`.

The process invoking the helper must pass the following as its args:

- `--endpoint` followed by the endpoint to connect to with TLS.
- `--port` followed by the port to use for the TCP connection to the endpoint.
- `--private-key` followed by the `system.privateKeyPath` config value.
- `--certificate` followed by the `system.certificateFilePath` config value.
- `--root-ca` followed by the `system.rootCaPath` config value.

The invoking process may additionally set the following args:

- `--proxy` followed by the proxy HTTP/HTTPS endpoint to use.

When passed a `--proxy` arg, the TLS helper MUST either use it for the
connection or exit with a non-zero error code.

The TLS helper will also get a control socket at file descriptor `3`. This will
be a unix domain socket. Except in case of an error, the helper MUST use the the
control socket to send a single message with the payload `"socket"`, and with
ancillary data of type `SCM_RIGHTS` with a single file descriptor for a socket
for the parent's TLS tunneled traffic. The control socket MUST NOT be read from.
The helper MUST NOT write to the socket more than once.

## Protocol

On an error, the TLS helper MUST exit with a non-zero error code.

On startup, the TLS helper MUST create a connection to the endpoint using mutual
TLS with the provided private key and certificate. It MUST allow the server to
be vaildated with the provided root CA, and it MAY use other root certificates
such as the system certificate store. It MAY connect through a proxy, and/or
handle the private key or certificate values specially (for example, allowing
for `pkcs11:` URIs for keys/certs stored with PKCS#11.

If successful, it MUST then provide the parent a socket for communicating over
that TLS tunnel. This MUST be done by calling `sendmsg()` on the control socket
with the data being the 6-byte string `"socket"` and the socket provided using
`SCM_RIGHTS`.

After successfully sending the socket, if the socket will continue to function
after the helper exits, the helper MAY exit with a zero error code.

The socket MUST match the state of the tunnel, i.e. when no more data can be
read, the socket must provide an end of file. If the parent shuts down the write
portion of its socket, this should be propagated. This is accomplished by
default if the socket returned to the parent is a TCP socket to the endpoint
with kTLS enabled. If the child, for example, instead provides a socket created
with `socketpair()` and handles encrypting/decrypting and forwarding between the
socketpair and a TCP socket to the endpoint, then it must shutdown the
socketpair socket's write end when reading an EOF from the TCP socket, and it
must shutdown the TCP socket's write end when reading an EOF from the socketpair
socket.

If the helper continues to run, once both the parent and endpoint have sent an
EOF, it SHOULD exit with a zero exit code.
