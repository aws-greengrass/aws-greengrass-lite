#!/bin/bash

gg_confdir="/etc/greengrass"

# Set default for uninstall to false
UNINSTALL=false


# Function to display usage
usage() {
    echo "Usage: $0 [-p package] [-k kit] [-u]"
    echo "  -p package  : Specify the greengrass-lite deb package file name (without any path, should be in the same dir)."
    echo "  -k kit      : Specify the Connection Kit file name (without any path, should be in the same dir)."
    echo "  -u          : Uninstall the greengrass-lite deb package and the created user, group and directories. Other parameters will be ignored"
    exit 1
}


# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root"
   exit 1
fi


# Function to check Ubuntu version
check_ubuntu_version() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        if [ "$ID" = "ubuntu" ] && [ "$VERSION_ID" = "24.04" ]; then
            return 0
        fi
    fi
    echo "Error: This greengrass lite package is only working with Ubuntu 24.04."
    echo "Current system: $PRETTY_NAME"
    exit 1
}


# Function to find default package and kit
find_defaults() {
    local -a deb_files
    mapfile -t deb_files < <(find . -maxdepth 1 -type f ! -name '.*' -regex ".*greengrass-lite.*\.deb" -print)

    local -a zip_files
    mapfile -t zip_files < <(find . -maxdepth 1 -type f ! -name '.*' -regex ".*connectionKit.*\.zip" -print)

    if [[ ${#deb_files[@]} -eq 1 && -f "${deb_files[0]}" ]]; then
        PACKAGE="${deb_files[0]}"
    elif [[ ${#deb_files[@]} -gt 1 ]]; then
        echo "Error: Multiple .deb files found. Please specify the package using the -p option."
        usage
    fi

    if [[ ${#zip_files[@]} -eq 1 && -f "${zip_files[0]}" ]]; then
        KIT="${zip_files[0]}"
    elif [[ ${#zip_files[@]} -gt 1 ]]; then
        echo "Error: Multiple .zip files found. Please specify the kit using the -k option."
        usage
    fi
}


# Only find_defaults if neither PACKAGE nor KIT is specified and we're not uninstalling
if [[ -z "$PACKAGE" && -z "$KIT" && "$UNINSTALL" != true ]]; then
    find_defaults
fi


# Parse command line arguments
while getopts ":p:k:u" opt; do
  case $opt in
    p) PACKAGE="$OPTARG"
    ;;
    k) KIT="$OPTARG"
    ;;
    u) UNINSTALL=true
    ;;
    \?) echo "Invalid option -$OPTARG" >&2
    usage
    ;;
  esac
done

# Check if package and kit are set
if [[ -z "$PACKAGE" || -z "$KIT" ]] && [[ "$UNINSTALL" != true ]]; then
    echo "Error: Both package and kit must be specified or found automatically."
    usage
fi

echo "Used parameters for script invocation:"
echo "Package: $PACKAGE"
echo "Kit: $KIT"
echo "Uninstall: $UNINSTALL"


# functions

# Function to check if aws-greengrass-lite is already installed
check_existing_installation() {
    if dpkg -s aws-greengrass-lite &> /dev/null; then
        echo "Error: aws-greengrass-lite is already installed."
        echo "If you want to reinstall, please uninstall first using the -u option."
        echo "If you want to just upgrade the aws-greengrass-lite package and "
        echo "keep configuration and components, run:"
        echo "sudo apt install ./aws-greengrass-lite-x.x.x-Linux.deb"
    fi
}

# Function to unzip the Connection Kit, modify kit specifc greengrass-lite config
process_connection_kit() {
    echo "Processing Connection Kit..."
    if [ -f "${KIT}" ]; then
        unzip -jo "${KIT}" -d "${gg_confdir}/"
        echo "Connection Kit unzipped to ${gg_confdir}/"

        sudo sed -i -e s:{{config_dir}}:\/etc\/greengrass:g -e s:{{data_dir}}:\/var\/lib\/greengrass:g -e s:{{nucleus_component}}:aws.greengrass.Nucleus-Lite:g ${gg_confdir}/config.yaml
    else
        echo "Error: Connection Kit file not found: ${KIT}"
        exit 1
    fi
}


# install
install() {
  set -euo pipefail

  check_ubuntu_version

  check_existing_installation

  apt update

  apt install -y zip

  apt install --reinstall  -y ./"${PACKAGE}"

  process_connection_kit

  echo "Installation completed."
  echo "To start greengrass-lite, run: systemctl start greengrass-lite.target"
  echo "To disable greengrass-lite at boot, run: systemctl disable greengrass-lite.target"
  echo "To check the status, run: systemctl status --with-dependencies greengrass-lite.target"
  echo "To just follow the logs, just run: journalctl -f"
}


# uninstall
uninstall() {
    echo "Uninstalling Greengrass Lite..."

    systemctl stop greengrass-lite.target
    systemctl disable greengrass-lite.target
    systemctl disable ggl.aws_iot_tes.socket
    systemctl disable ggl.aws_iot_mqtt.socket
    systemctl disable ggl.gg_config.socket
    systemctl disable ggl.gg_health.socket
    systemctl disable ggl.gg_fleet_status.socket
    systemctl disable ggl.gg_deployment.socket
    systemctl disable ggl.gg_pubsub.socket
    systemctl disable ggl.gg-ipc.socket.socket
    systemctl disable ggl.core.ggconfigd.service
    systemctl disable ggl.core.iotcored.service
    systemctl disable ggl.core.tesd.service
    systemctl disable ggl.core.ggdeploymentd.service
    systemctl disable ggl.core.gg-fleet-statusd.service
    systemctl disable ggl.core.ggpubsubd.service
    systemctl disable ggl.core.gghealthd.service
    systemctl disable ggl.core.ggipcd.service
    systemctl daemon-reload

    apt remove -y --purge aws-greengrass-lite

    echo "Uninstallation completed."
}


# main loop
if [ "$UNINSTALL" = true ]; then
  uninstall
else
  install
fi
