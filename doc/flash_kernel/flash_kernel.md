<!-- ..  SPDX-License-Identifier: Apache-2.0
    Copyright (c) 2025 Debojeet Das. -->

# Introduction

This documents describes how to install flash linux out-of-tree kernel in a system.

## System Requirements and Building flash kernel

flash kernel is based on linux kernel v6.10.6. This documentation assumes that you are building and installing the kernel on a
Ubuntu 24.04 LTS host. Other distributions may work, but this documentation assumes Ubuntu.

## Clone the flash kernel repository

```
git clone https://github.com/rickydebojeet/linux.git
```

Make sure to note the path to the flash kernel repository. This path will be used in the next steps.

## Install the kernel automatically

Use the following steps to install the kernel automatically.

```
sudo ./usertools/flash_kernel/install.sh <path/to/flash_kernel [nproc]
```
The script will install the kernel and the required dependencies. The script will also install the kernel modules.

The path to the flash kernel repository is required as the first argument. The optional second argument is the number of processors to use for building the kernel.
If the second argument is not provided, the script will use only one processor to build the kernel.

## Install the kernel manually

**Prerequisites:** 

To build the kernel, you need to install few packages that are required:

```
sudo apt update
sudo apt install build-essential libncurses-dev bison flex libssl-dev libelf-dev fakeroot dwarves
```
Now you need to run the following commands:

```
cd <path/to/flash_kernel>
cp -v /boot/config-$(uname -r) .config
(yes "" || true) | make localmodconfig
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
scripts/config --set-str CONFIG_SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str CONFIG_SYSTEM_REVOCATION_KEYS ""
```

Now you can build the kernel:

```
make -j$(nproc)
```

After the kernel is built, you can install it:

```
sudo make modules_install
sudo make install
```

After the kernel is installed, you just need to reboot the system:

```
sudo reboot
```