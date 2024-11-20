#!/bin/bash

gg_user="ggc_user"
gg_group="ggc_group"
gg_rundir_relative="aws-greengrass-v2"
gg_workingdir="/var/lib/aws-greengrass-v2"
gg_confdir="/etc/greengrass"
gg_bindir="/usr/bin"
service_file="/lib/systemd/system/greengrass-lite.service"
config_file="${gg_confdir}/config.d/greengrass-lite.yaml"


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


# Function to create a group if it doesn't exist
create_group() {
    if ! getent group "$1" > /dev/null 2>&1; then
        groupadd "$1"
        echo "Group $1 created."
    else
        echo "Group $1 already exists."
    fi
}


# Function to create a user if it doesn't exist
create_user() {
    if ! id "$1" &>/dev/null; then
        useradd -m -g "$2" "$1"
        echo "User $1 created and added to group $2."
    else
        echo "User $1 already exists."
    fi
}


# Function to create systemd service file
create_service_file() {
    cat > "${service_file}" << EOL
[Unit]
Description=greengrass lite service

[Service]
ExecStart=${gg_bindir}/run_nucleus
Type=simple
User=${gg_user}
Group=${gg_group}
Restart=on-failure
WorkingDirectory=${gg_workingdir}
StateDirectory=${gg_rundir_relative}

[Install]
WantedBy=multi-user.target
EOL
    echo "Systemd service file created at $service_file"
}


# Function to create the configuration file
create_config_file() {
    cat > "$config_file" << EOL
---
system:
  rootPath: "${gg_workingdir}"
services:
  aws.greengrass.Nucleus-Lite:
    componentType: "NUCLEUS"
    configuration:
      runWithDefault:
        posixUser: "${gg_user}:${gg_group}"
      greengrassDataPlanePort: "8443"
      tesCredUrl: "http://127.0.0.1:8080/"
EOL
    echo "Configuration file created at $config_file"
}


# Function to unzip the Connection Kit, modify kit specifc greengrass-lite config
process_connection_kit() {
    echo "Processing Connection Kit..."
    if [ -f "${KIT}" ]; then
        unzip -jo "${KIT}" -d "${gg_confdir}/"
        echo "Connection Kit unzipped to ${gg_confdir}/"

        sudo sed -i -e s:{{config_dir}}:\/etc\/greengrass:g -e s:{{data_dir}}:\/var\/lib\/aws-greengrass-v2:g -e s:{{nucleus_component}}:aws.greengrass.Nucleus-Lite:g ${gg_confdir}/config.yaml
    else
        echo "Error: Connection Kit file not found: ${KIT}"
        exit 1
    fi
}


# Function to create sudoers file
create_sudoers_file() {
    local sudoers_dir="/etc/sudoers.d"
    local sudoers_file="${sudoers_dir}/greengrass-lite"

    # Create the sudoers file with the specified content
    echo "${gg_user} ALL=(root) NOPASSWD:/usr/bin/systemctl *" > "${sudoers_file}"

    # Set correct permissions for the sudoers file
    chmod 0440 "${sudoers_file}"

    echo "Sudoers file created at ${sudoers_file}"
}


# install
install() {
  set -euo pipefail

  check_ubuntu_version

  check_existing_installation

  apt update

  apt install -y zip

  apt install -y ./"${PACKAGE}"

  mkdir -p "${gg_confdir}"/config.d

  create_group "${gg_group}"

  create_user "${gg_user}" "${gg_group}"

  mkdir "${gg_workingdir}"

  chown "${gg_user}":"${gg_group}" "${gg_workingdir}"

  process_connection_kit

  create_service_file

  create_config_file

  create_sudoers_file

  systemctl daemon-reload

  systemctl enable greengrass-lite.service

  systemctl start greengrass-lite

  echo "Installation completed."
  echo "To check the logs, run: journalctl -u greengrass-lite -f"
}


# uninstall
uninstall() {
    echo "Uninstalling Greengrass Lite..."

    systemctl disable greengrass-lite.ser.github/workflows/build-packages-multi-platform-container-qemu/install-greengrass-lite.shvice

    systemctl stop greengrass-lite

    echo "Removing systemd service file..."
    rm -f "$service_file"

    apt remove -y aws-greengrass-lite

    echo "Removing configuration directory..."
    if [ -z "${gg_confdir}" ]; then
        echo "Error: gg_confdir is not set. Aborting removal of configuration directory."
        exit 1
    fi
    rm -rf "${gg_confdir}"

    echo "Removing user ${gg_user}..."
    userdel "${gg_user}"

    echo "Removing group ${gg_group}..."
    groupdel "${gg_group}"

    if [ -z "${gg_workingdir}" ]; then
        echo "Error: gg_workingdir is not set. Aborting removal of working directory."
        exit 1
    fi
    echo "Removing working directory..."
    rm -rf "${gg_workingdir}"

    systemctl daemon-reload

    echo "Removing sudoers file..."
    rm -f "/etc/sudoers.d/greengrass-lite"

    echo "Uninstallation completed."
}


# main loop
if [ "$UNINSTALL" = true ]; then
  uninstall
else
  install
fi
