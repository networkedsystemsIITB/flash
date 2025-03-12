#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das.
#
# Build and install the flash kernel 

set -e -o pipefail

if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit
fi

if [ "$1" = "" ]; then
    echo "Provide the kernel directory as an argument"
    echo "Usage: $0 <kernel_dir> [nproc]"
    echo "[nproc] is optional"
    echo "Example: $0 /path/to/kernel 7"
    exit 1
fi

set -xu

# Take the kernel directory as an argument
KERNEL_DIR=$1

NPROC=1
if [ "$2" != "" ]; then
    NPROC=$2
fi

# Change to kernel directory
cd $KERNEL_DIR

printf "Installing dependencies...\n"
sudo apt install -y build-essential libncurses-dev bison flex libssl-dev libelf-dev fakeroot dwarves

# Configure kernel
printf "Configuring kernel...\n"
cp -v /boot/config-$(uname -r) .config
(yes "" || true) | make localmodconfig

scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
scripts/config --set-str CONFIG_SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str CONFIG_SYSTEM_REVOCATION_KEYS ""

# Compile the kernel
printf "Compiling kernel...\n"
make -j$(nproc)

# Install the kernel
printf "Installing kernel...\n"
sudo make modules_install
sudo make install

set +xu

if [ -z "$(awk -F\' '/menuentry / {print $2}' /boot/grub/grub.cfg | grep -m 1 'Ubuntu, with Linux 6.10.6-flash+')" ]; then
    printf "Cannot find flash kernel. Please install the kernel manually.\n"
    exit 1
fi

printf "flash kernel is installed. To boot into flash kernel, please reboot the system:\n"
printf "    sudo reboot\n"