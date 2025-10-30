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
    echo "Usage: $0 <kernel_dir> [nproc]"
    echo ""
    echo "<kernel_dir> is the path to the kernel source directory"
    echo "[nproc] is the number of parallel jobs to use for compilation. (default: number of CPU cores)"
    echo ""
    echo "Example: $0 /path/to/kernel 2"
    exit 1
fi

set -u

# Take the kernel directory as an argument
KERNEL_DIR=$1

NPROC=$(nproc)
if [ -n "${2:-}" ]; then
    NPROC="$2"
fi

echo "Using $NPROC parallel jobs for compilation..."

# Change to kernel directory
cd "$KERNEL_DIR"

echo "Installing dependencies..."
sudo apt install -y build-essential libncurses-dev bison flex libssl-dev libelf-dev fakeroot dwarves bc

# Prompt if the user wants quick build
echo -n "Do you want to perform a quick build? (Y/n) "
read -r QUICK_BUILD
if [[ "$QUICK_BUILD" == "n" || "$QUICK_BUILD" == "N" ]]; then
    echo "Performing full configuration..."
    make olddefconfig
else
    echo "Configuring kernel with quick build..."
    cp -v /boot/config-$(uname -r) .config
    (yes "" || true) | make localmodconfig  
fi

scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
scripts/config --set-str CONFIG_SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str CONFIG_SYSTEM_REVOCATION_KEYS ""

GCC_MAJOR=$(gcc -dumpfullversion -dumpversion | cut -d. -f1)
EXTRA_FLAGS=""
if [ "$GCC_MAJOR" -ge 15 ]; then
    echo "GCC $GCC_MAJOR detected — installing GCC 14..."
    sudo apt install -y gcc-14 g++-14
    echo "Using GCC-14 for kernel build..."
    EXTRA_FLAGS="CC=gcc-14 HOSTCC=gcc-14"    
fi

# Compile the kernel
echo "Compiling kernel..."
make -j"$NPROC" $EXTRA_FLAGS

# Install the kernel
echo "Installing kernel..."
sudo make modules_install $EXTRA_FLAGS
sudo make install $EXTRA_FLAGS

set +xu

FLASH_KERNEL=$(awk -F"'" '/menuentry / {print $2}' /boot/grub/grub.cfg \
    | grep -m 1 -E 'Ubuntu, with Linux 6\.10\.6-[0-9.]+-flash\+')

if [ -z "$FLASH_KERNEL" ]; then
    echo "Cannot find flash kernel. Please install the kernel manually."
    exit 1
fi

echo "flash kernel is installed."
echo "To boot into flash kernel immediately, run:"
echo
echo -e "    sudo grub-reboot \"Advanced options for Ubuntu>$(echo "$FLASH_KERNEL")\""
echo -e "    sudo reboot now"
echo
