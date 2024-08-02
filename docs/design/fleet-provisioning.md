# `fleet-provisioning` design

See [`fleet-provisioning` spec](../spec/executable/fleet-provisioning.md) for
the public interface for `fleet-provisioning`.

The fleet provisioning is designed to be used only once during the intital
installation of greengrass. This is because each time the binary is run it will
create a new certificate and update the database with appropriate values.

## Dependencies

The library depends on 2 major gg services:

- ggconfigd
- iotcored

For application to startup it requires `ggconfigd` running, it can be a
temporary new instance if needed but the database must remain the same for other
application will be depending on the values contained within it. As for iotcored
a new instance of it will be created with claim certs. This it for the
application makes a MQTT call to IOTCore requesting a certificate based on a
csr.

## Implementation details

FleetProvisioner will first look in the database look for the following values:

- template name
- aws data endpoint
- claim private key path
- claim certificate path

once the information is fetched from the database it will start a new instance
of iotcored with claim certificates, using fork-exec. Once a new instance is
setup application then will use openssl to generate a private key, public key
and a CSR, all of these will be created at the current working directory unless
cli parameters dictate otherwise. After this application will open a channel to
communicate with claim-iotcored. Then a call to the aws's MQTT endpoint is made
to fetch the Certificate based on the CSR. With the way AWS's MQTT endpoint work
we will be listining on a different endpoints for the response, /accepted if it
was successful and /rejected if it failed. More can be found
[here](https://docs.aws.amazon.com/iot/latest/developerguide/fleet-provision-api.html).

The same process will be repeated for Registering the thing as well. Once, done
the new set of

- thing name,
- private-certificate path
- device certificate's path

will updated to the database, concluding the provisioning process.
