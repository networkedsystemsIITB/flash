# FLASH Userspace Library

<img align="right" width="230" height="230" src="https://www.cse.iitb.ac.in/~debojeetdas/flash/flash.png">

[![Commitizen friendly](https://img.shields.io/badge/commitizen-friendly-brightgreen.svg)](http://commitizen.github.io/cz-cli/)
![Ubuntu 24.04](https://github.com/rickydebojeet/flash/actions/workflows/main.yml/badge.svg)

FLASH: Fast Linked AF_XDP Sockets for High Performance Network Services

A userspace library that lets you link isolated unprivileged AF_XDP network functions to boost performance using FLASH out-of-tree kernel. It’s also great for deploying network functions in containers without needing a custom kernel, but without chaining support.

## Baremetal Usage Instructions

Baremetal Deployment has been tested on Ubuntu 24.04 hosts and is expected to function similarly on Ubuntu 22.04 hosts. However, it may encounter build failures on Ubuntu 20.04 and older versions due to the absence of necessary libraries in the apt repository.
For standalone NFs operations, Linux kernel versions 5.17.5 and later are recommended.

### Building

The library is built on top of libbpf and libxdp. You can install the dependencies using the following commands:

```bash
sudo apt install -y build-essential meson libbpf-dev pkg-config git gcc-multilib clang llvm lld m4 libpcap-dev libcjson-dev libncurses-dev
```

The libxdp library is not available in the Ubuntu repositories. You can build it from source using the following commands:

```bash
git clone https://github.com/xdp-project/xdp-tools.git
make -j -C xdp-tools libxdp
sudo PREFIX=/usr make -j -C xdp-tools libxdp_install
```

Once you have installed the dependencies, you can build the library using the following commands:

```bash
make
```

### Usage

The library offers a straightforward API for constructing and executing AF_XDP. Applications may utilize the library to construct and operate AF_XDP sockets either through the FLASH monitor, which manages the control plane, or directly via the library. The monitor enables applications to execute multiple AF_XDP applications simultaneously, whether sharing memory in privileged or non-privileged modes.

#### Using Monitor

```bash
sudo ./build/monitor/monitor 
```

A TUI will be initiated, allowing configuration parameters to be passed to setup AF_XDP setups and chains. Configurations will be stored in a JSON file and loaded/unloaded on demand. Once the monitor has started and the configuration is properly set, NFs can commence running without the need for any privileges. A sample NF usage instruction is provided below.

```bash
./build/examples/l2fwd/l2fwd -u 0 -f 1 -ax -- -s 0 -c 2 -e 3
```

## Docker Usage Instructions

Docker containers enable the consistent and isolated deployment of NFs in a portable development environment, facilitating the entry of NF developers into the setup process. To begin, you can create an image of the FLASH container.  

```bash
# For NFs
make docker

# For Monitor
make docker_mon
```

It is noteworthy that in this setup, the monitor should be placed within a network namespace where the network interface (NIC) that the NFs will utilize is present. Additionally, the monitor requires root privileges to facilitate deployment. However, NFs can be isolated within unprivileged containers. The monitor can be initiated in the host otherwise within a privileged container using the following command:

```bash
docker run -rm -it --privileged -v /tmp/flash/:/tmp/flash/ --net=host flash:mon ./build/monitor/monitor
```

 If the monitor is ready and running, the NF can be initiated using the following command:

```bash
docker run --rm -it -v /tmp/flash/:/tmp/flash/ flash:dev ./build/examples/l2fwd/l2fwd -u 0 -f 1 -ax -- -s 0 -c 2 -e 3
```

> `/tmp/flash` contains a UDS socket that is used by NFs to communicate with the monitor.

You can also use docker compose to deploy multiple NFs at the same time.

```bash
docker compose up -d
```

### Chaining NFs using FLASH Monitor

To chain NFs you need to install a custom out-of-tree kernel. Checkout the instructions [here](./doc/flash_kernel/flash_kernel.rst).
