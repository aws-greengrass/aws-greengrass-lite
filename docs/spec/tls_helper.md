# Greengrass Lite TLS helper API

AWS Greengrass Lite will allow TLS communication support to be pluggable through
the TLS helper interface. This allows customers to plug in custom TLS setups,
such as using different TLS library, custom HSM setups, etc.

It will be called from a library module that will implement a C TLS interface on
top of it and handle proxy, etc.

## Requirements

The TLS interface is designed to meet the following requirements:

- Allow TLS support to be pluggable with custom implementations.
- Enable writing an implementation without a library dependency.
- Keep TLS libraries out of the main daemon process (potentially reduces memory
  usage).
- Allow efficient transport using kTLS.

## Interface

The TLS helper must be on the PATH for the Greengrass nucleus daemons. The
binary name must be `ggl-tls-helper`. Greengrass nucleus daemons will invoke it
by executing `ggl-tls-helper`.

The helper will be passed the following as its args:

- `--private-key` followed by the `system.privateKeyPath` config value.
- `--certificate` followed by the `system.certificateFilePath` config value.
- `--root-ca` followed by the `system.rootCaPath` config value.
- `--hostname` followed by the hostname to use for SNI (Server Name Indication).

The TLS helper should not handle proxying. Ignore proxy environment variables
(`ALL_PROXY`, `HTTP_PROXY`, `HTTPS_PROXY`, and `NO_PROXY`).

The TLS helper will get a control socket at file descriptor `3`. This will be a
unix domain socket. Except in case of an error, the helper MUST use the control
socket to send a single message with the payload `"socket"`, and with ancillary
data of type `SCM_RIGHTS` with a single file descriptor for a socket for the
parent's TLS tunneled traffic. The control socket MUST NOT be read from. The
helper MUST NOT write to the socket more than once.

The TLS helper will get an already-connected socket at file descriptor `4`. The
helper MUST perform TLS over this socket. The parent is responsible for
establishing the underlying transport. The TLS helper cannot assume that the
socket is a TCP socket.

## Protocol

On an error, the TLS helper MUST exit with a non-zero error code.

On startup, the TLS helper MUST perform a mutual TLS handshake over the socket
at fd 4 using the provided private key and certificate. It MUST validate the
server using the provided root CA. The `--hostname` value MUST be used for SNI
(Server Name Indication). It MAY handle the private key or certificate values
specially (for example, allowing for `pkcs11:` URIs for keys/certs stored with
PKCS#11).

If successful, it MUST then provide the parent a socket for communicating over
that TLS tunnel. This MUST be done by calling `sendmsg()` on the control socket
with the data being the 6-byte string `"socket"` and the socket provided using
`SCM_RIGHTS`.

After successfully sending the socket, if the socket will continue to function
after the helper exits (such as if the returned socket is a TCP socket with kTLS
enabled for both rx and tx), the helper MAY exit with a zero error code.

The socket MUST match the state of the tunnel, i.e. when no more data can be
read, the socket must provide an end of file. If the parent shuts down the write
portion of its socket, this should be propagated. This is accomplished by
default if the socket returned to the parent is a TCP socket to the endpoint
with kTLS enabled. If the child, for example, instead provides a socket created
with `socketpair()` and handles encrypting/decrypting and forwarding between the
socketpair and the fd 4 socket, then it must shutdown the socketpair socket's
write end when reading an EOF from the fd 4 socket, and it must shutdown the fd
4 socket's write end when reading an EOF from the socketpair socket.

If the helper continues to run, once both the parent and endpoint have sent an
EOF, it SHOULD exit with a zero exit code.
