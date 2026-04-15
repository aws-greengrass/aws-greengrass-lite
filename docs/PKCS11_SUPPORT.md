# PKCS#11 Support

Greengrass Lite supports PKCS#11 URIs for `privateKeyPath` and
`certificateFilePath`. This allows the device private key (and optionally the
certificate) to be stored in a hardware security module or software token rather
than as files on disk.

Greengrass Lite uses the OpenSSL
[OSSL_STORE](https://www.openssl.org/docs/man3.0/man7/ossl_store.html) API to
load keys and certificates. When an OpenSSL PKCS#11 provider is configured, any
`pkcs11:` URI is handled transparently.

## OpenSSL Configuration

You need an OpenSSL 3.x provider that bridges PKCS#11. Install the provider and
your PKCS#11 module. For example, on Ubuntu with SoftHSM:

```bash
sudo apt install openssl pkcs11-provider softhsm2 opensc
```

Edit `/etc/ssl/openssl.cnf` to register the pkcs11 provider alongside the
default provider. Ensure the top-level initialization hook is present (some
distributions include this by default):

```ini
openssl_conf = openssl_init

[openssl_init]
providers = provider_sect
```

In the `[provider_sect]` section, add a `pkcs11` entry:

```ini
[provider_sect]
default = default_sect
pkcs11 = pkcs11_sect
```

Ensure the default provider is activated:

```ini
[default_sect]
activate = 1
```

Add the pkcs11 provider section at the end of the file, pointing
`pkcs11-module-path` at your PKCS#11 module (`.so`):

```ini
[pkcs11_sect]
module = /usr/lib/x86_64-linux-gnu/ossl-modules/pkcs11.so
pkcs11-module-path = /usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so
activate = 1
```

Adjust the paths for your platform. The `module` path is the OpenSSL
pkcs11-provider shared object; `pkcs11-module-path` is the PKCS#11 module for
your HSM.

The PIN can also be configured here instead of in the URI, using
`pkcs11-module-token-pin`:

```ini
[pkcs11_sect]
pkcs11-module-token-pin = 1234
```

Verify the provider loads:

```bash
openssl list -providers
```

You should see both `default` and `pkcs11` listed as active.

## Setting Up Credentials

Generate the private key directly on the HSM so it never exists as a file.
Create a CSR from the HSM-resident key, then use the CSR to obtain a certificate
from AWS IoT.

```bash
# Create CSR using the PKCS#11 key
openssl req -new -key "pkcs11:token=ggl;object=key;pin-value=1234" \
  -out device.csr -subj "/CN=MyThing"
```

Submit `device.csr` to AWS IoT to obtain a certificate:

```bash
aws iot create-certificate-from-csr \
  --certificate-signing-request file://device.csr \
  --set-as-active \
  --certificate-pem-outfile device.pem
```

Import the certificate into the token using your HSM's tooling.

The user running Greengrass Lite (`ggcore`) must have read/write access to the
token files. For SoftHSM, add `ggcore` to the `softhsm` group:

```bash
sudo usermod -aG softhsm ggcore
```

## Greengrass Configuration

Set `privateKeyPath` and `certificateFilePath` to PKCS#11 URIs:

```yaml
---
system:
  privateKeyPath: "pkcs11:token=ggl;object=key;pin-value=1234"
  certificateFilePath: "pkcs11:token=ggl;object=cert;pin-value=1234"
  rootCaPath: "/path/to/AmazonRootCA1.pem"
  thingName: "MyThing"
```

The `rootCaPath` remains a file path — only the key and certificate support
PKCS#11 URIs.

## Verifying

You can test the PKCS#11 credentials independently with `openssl s_client`:

```bash
openssl s_client \
  -connect YOUR_IOT_ENDPOINT:8443 \
  -key "pkcs11:token=ggl;object=key;pin-value=1234" \
  -cert "pkcs11:token=ggl;object=cert;pin-value=1234" \
  -CAfile /path/to/AmazonRootCA1.pem \
  -brief </dev/null
```

A successful connection shows `CONNECTION ESTABLISHED` and `Verification: OK`.

## Troubleshooting

### Provider Not Found

If `openssl list -providers` does not show the pkcs11 provider:

- Verify `openssl_conf = openssl_init` is at the top of `openssl.cnf`
- Check the `module` path points to the correct provider `.so`
- Run `find /usr -name "pkcs11.so" -o -name "pkcs11prov.so"` to locate it

### Permission Denied

If Greengrass Lite fails to access the token:

- Verify `ggcore` can access the PKCS#11 module's token storage
- For SoftHSM, ensure `ggcore` is in the `softhsm` group and that token files
  are group-readable/writable
- Log out and back in after group changes

### Token Integrity Check Failed

SoftHSM reports this when token files were modified by a different user or
process. Ensure the token is created by the same user that will access it, or
that file ownership and permissions are consistent.
