# FLASH: Fast Linked AF_XDP Sockets for High Performance Network Services

<img align="right" width="200" height="200" src="https://www.cse.iitb.ac.in/~debojeetdas/flash/flash.png">

[![Commitizen friendly](https://img.shields.io/badge/commitizen-friendly-brightgreen.svg)](http://commitizen.github.io/cz-cli/)
![Ubuntu 24.04](https://github.com/rickydebojeet/flash/actions/workflows/main.yml/badge.svg)

FLASH is a high-speed userspace library that makes it easy to build efficient, unprivileged AF_XDP applications for modern cloud and edge deployments.

Seamlessly integrated with the **FLASH kernel**, it extends AF_XDP to enable true zero-copy packet sharing between network functions (NFs) and network devices, unlocking performance that surpasses traditional AF_XDP chaining solutions.

## Key Features
- **Zero-Copy Packet Sharing**: Unlock unparalleled throughput and minimal latency with zero-copy data paths between NFs and network devices.
- **Unprivileged Operation**: Run AF_XDP applications securely without root access simplifying deployment while maintaining isolation.
- **Packet Isolation**: Ensure strong packet-level isolation between NFs, even when sharing memory powered by Rust and FLASH kernel safeguards.
- **Backward Compatibility**: Chain existing AF_XDP applications in copy-based mode with no code changes — easy migration, no disruption.
- **Flexible Deployment Options**: Deploy seamlessly on bare metal or in containers for consistent, isolated environments. Works on standard Linux kernels too (without zero-copy chaining support).
- **Multi tenant Support**: Designed for shared environments — the OS remains in control of resources, ensuring safety and fairness when multiple users or tenants share the same host. Unlike DPDK, FLASH plays nicely in multi-tenant and cloud-native setups.

## Getting Started

Clone the repositories and install the FLASH kernel for zero-copy chaining support.
```bash
git clone https://github.com/networkedsystemsIITB/flash.git
git clone https://github.com/networkedsystemsIITB/flash-linux.git
cd flash
sudo ./usertools/flash_kernel/install.sh ../flash-linux
```

The `install.sh` script will build and install the kernel along with its modules.  
It requires the path to the flash-linux repository as the first argument.  
An optional second argument can be provided to specify the number of processors to use during the build.

During execution, the script will prompt you to choose between a quick build and a full build. Select the quick build option for faster compilation.

Follow the on-screen instructions provided in the terminal after installation to boot into the FLASH kernel.

For more details, refer to the [FLASH Kernel Guide](./doc/flash_kernel/flash_kernel.md).

### Building the userspace library and examples

FLASH has been tested on Ubuntu 24.04 and is expected to work similarly on Ubuntu 22.04.  
Older versions (e.g., Ubuntu 20.04 or earlier) may encounter build issues due to missing dependencies.

> Recommended Kernel Version: 5.17.5 or later for standalone NF operations.

Install the required dependencies using the following command:

```bash
sudo apt install -y build-essential meson libbpf-dev pkg-config git gcc-multilib clang llvm lld m4 libpcap-dev libcjson-dev libncurses-dev libnuma-dev
```

`libxdp` is not included in Ubuntu repositories — build it from source:

```bash
git clone https://github.com/xdp-project/xdp-tools.git
make -j -C xdp-tools libxdp
sudo PREFIX=/usr make -j -C xdp-tools libxdp_install
```

Install Rust for using the Rust components of FLASH:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

Once dependencies are ready, build the library and examples:

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
