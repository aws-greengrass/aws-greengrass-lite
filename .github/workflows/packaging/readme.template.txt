###############################################################################
# AWS Greengrass Lite
###############################################################################

AWS IoT Greengrass runtime for constrained devices.

The Greengrass Lite nucleus provides a smaller alternative to the Classic
nucleus for Greengrass v2 deployments.

Greengrass Lite aims to maintain compatibility with the Classic nucleus, and
implements a subset of its functionality.

There is a arm64 and x86-64 version available of this zip file. For the latest
version, as well as other install options check here:
https://github.com/aws-greengrass/aws-greengrass-lite/releases

This deb package only works with Ubuntu 24.04., because of library version,
package constraints! It doesn't matter if this Ubuntu is running on EC2,
Desktop, Raspberry Pi, Container...

This package has been built from this source revision:
{{ VERSION_LINK }}

###############################################################################
# INSTALLATION
###############################################################################

copy *.deb package and the install-greengrass-lite.sh from this zip onto your
device (e.g. scp, usb stick, mounting fat partition (Raspberry Pi))

copy ConnectionKit zip onto your device (e.g. scp, usb stick, mounting fat
partition (Raspberry Pi))

The three files should be in the same folder on the device.

On the device run installation script, this will automatically detect a *.deb
and *.zip for installation in the same dir:

sudo ./install-greengrass-lite.sh

###############################################################################
# UNINSTALLATION
###############################################################################

Add a parameter "-u" to the script. Be careful this will delete /etc/greengrass
and /var/lib/greengrass!

sudo ./install-greengrass-lite.sh -u

###############################################################################
# UPGRADE
###############################################################################

When a new version of greengrass lite is available and you want to keep your
configuration and components. Install just the deb package from the zip, without
using the (initial-) installation script.

sudo apt install ./aws-greengrass-lite-x.x.x-Linux.deb

###############################################################################
# LICENSE
###############################################################################
