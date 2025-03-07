# FLASH Userspace Library

Flash userspace library for AF_XDP network function chaining.

## Building

The library is built on top of libbpf and libxdp. You can install the dependencies using the following commands:

```bash
sudo apt install -y build-essential meson libbpf-dev pkg-config git gcc-multilib clang llvm lld m4 libpcap-dev libcjson-dev libncurses-dev
```

The libxdp library is not available in the Ubuntu repositories. You can build it from source using the following commands:

```bash
git clone https://github.com/xdp-project/xdp-tools.git
cd xdp-tools/
make
sudo make install
```

Once you have installed the dependencies, you can build the library using the following commands:

```bash
meson setup builddir
cd builddir
meson compile
```

## Usage

The library provides a simple API to build and run AF_XDP. Applications can use the library to create and run AF_XDP sockets either using the FLASH monitor that handled the control plane or by directly using the library. Monitor allows applications to run multiple AF_XDP applications with or without sharing memory in non-privileged mode.

### Chaining NFs using FLASH Monitor

To chain NFs you need to install a custom out-of-tree kernel. Checkout the instructions [here](./doc/flash_kernel/flash_kernel.rst).
