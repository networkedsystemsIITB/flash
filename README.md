# FLASH Userspace Library

[![Commitizen friendly](https://img.shields.io/badge/commitizen-friendly-brightgreen.svg)](http://commitizen.github.io/cz-cli/)
![Ubuntu 24.04](https://github.com/rickydebojeet/flash/actions/workflows/main.yml/badge.svg)

Flash userspace library for AF_XDP network function chaining.

## Building

The library is built on top of libbpf and libxdp. You can install the dependencies using the following commands:

```bash
sudo apt install -y build-essential meson libbpf-dev pkg-config git gcc-multilib clang llvm lld m4 libpcap-dev libcjson-dev libncurses-dev
```

The libxdp library is not available in the Ubuntu repositories. You can build it from source using the following commands:

```bash
git clone https://github.com/xdp-project/xdp-tools.git
make -C xdp-tools
sudo make install -C xdp-tools
```

Once you have installed the dependencies, you can build the library using the following commands:

```bash
meson setup build
meson compile -C build
```

## Usage

The library provides a simple API to build and run AF_XDP. Applications can use the library to create and run AF_XDP sockets either using the FLASH monitor that handled the control plane or by directly using the library. Monitor allows applications to run multiple AF_XDP applications with or without sharing memory in non-privileged mode.

### Chaining NFs using FLASH Monitor

To chain NFs you need to install a custom out-of-tree kernel. Checkout the instructions [here](./doc/flash_kernel/flash_kernel.rst).
