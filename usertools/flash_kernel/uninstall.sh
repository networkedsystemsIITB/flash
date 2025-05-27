#!/bin/bash
# This script uninstalls flash kernel and its associated files.

# Check if the script is run as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi

# List all installed kernels
echo "Installed kernels:"
ls -1 /boot/vmlinuz-*
echo "-------------------------------------"

# Which kernel to remove
echo "Which kernel do you want to remove?"
echo "Please enter the version (e.g., 6.10.6-flash):"
read -r kernel_version

echo "Warning: This will remove the kernel $kernel_version and all its associated files."
echo "Are you sure you want to proceed? (y/n)"
read -r confirmation
if [[ "$confirmation" != "y" && "$confirmation" != "Y" ]]; then
    echo "Operation cancelled."
    exit 0
fi
echo "Removing kernel $kernel_version..."

# Check if the files exist before attempting to remove them
if ls /boot/vmlinuz-"$kernel_version"* 1> /dev/null 2>&1; then
    echo "Removing kernel files..."
    rm -f /boot/vmlinuz-"$kernel_version"*
else
    echo "Kernel files not found."
fi

if ls /boot/initrd-"$kernel_version"* 1> /dev/null 2>&1; then
    echo "Removing initrd files..."
    rm -f /boot/initrd-"$kernel_version"*
else
    echo "Initrd files not found."
fi

if ls /boot/System.map-"$kernel_version"* 1> /dev/null 2>&1; then
    echo "Removing System.map files..."
    rm -f /boot/System.map-"$kernel_version"*
else
    echo "System.map files not found."
fi

if ls /boot/config-"$kernel_version"* 1> /dev/null 2>&1; then
    echo "Removing config files..."
    rm -f /boot/config-"$kernel_version"*
else
    echo "Config files not found."
fi

if ls /lib/modules/"$kernel_version"* 1> /dev/null 2>&1; then
    echo "Removing modules files..."
    rm -rf /lib/modules/"$kernel_version"*
else
    echo "Modules files not found."
fi

if ls /var/lib/initramfs/"$kernel_version"* 1> /dev/null 2>&1; then
    echo "Removing initramfs files..."
    rm -rf /var/lib/initramfs/"$kernel_version"*
else
    echo "Initramfs files not found."
fi

sudo update-grub2

echo "Kernel $kernel_version uninstalled successfully."
echo "Please reboot your system to apply changes."
