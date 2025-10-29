<!-- SPDX-License-Identifier: Apache-2.0 Copyright (c) 2025 Debojeet Das. -->

# FLASH Kernel Guide

The **FLASH Kernel** extends the Linux **AF_XDP** subsystem to provide zero-copy packet redirection and efficient in-kernel data paths for high-performance user-space networking.

It introduces:
- A high-speed, in-kernel redirection mechanism between AF_XDP sockets.
- A sysfs control interface for managing AF_XDP sockets securely from user space.

#### Quick Links  
⚙️ [Kernel Installation](#kernel-installation)  
🧩 [Sysfs Interface](#sysfs-interface-for-af_xdp-socket-management)  
🔁 [Interrupt vs. Busy-Polling (`poll()` Usage)](#using-poll-for-interrupt-vs-busy-polling-mode)  
🧠 [Backpressure Handling (`poll()` & `recvfrom()`)](#backpressure-handling-with-poll-and-recvfrom)  
🚦 [TX Tracking per Flow (HOL Mitigation)](#tx-tracking-per-flow-mitigating-head-of-line-blocking)  
🧹 [Uninstalling the FLASH Kernel](#uninstalling-the-flash-kernel)  


## System Requirements

The FLASH kernel is based on Linux kernel v6.10.6. This documentation assumes that you are building and installing the kernel on an Ubuntu host. Other distributions may work, but this documentation assumes Ubuntu.

We have tested the FLASH kernel on the following Ubuntu versions:
- Ubuntu 22.04 LTS
- Ubuntu 24.04 LTS
- Ubuntu 25.04 
- Ubuntu 25.10

> **Note:** Ubuntu 25.10 and newer ship with **GCC 15**, which is incompatible with the FLASH kernel.  
> Install GCC 14 before building to avoid compilation errors.

## Kernel Installation

### Automated Installation (Recommended)

You can use the `install.sh` script to build and install the FLASH kernel automatically.

```bash
git clone https://github.com/networkedsystemsIITB/flash.git
git clone https://github.com/networkedsystemsIITB/flash-linux.git
cd flash
sudo ./usertools/flash_kernel/install.sh ../flash-linux
```

You’ll be prompted to choose between:

- **Quick Build:**
Compiles only currently loaded modules. Faster but limited, hot-plugging new devices or  using applications that require additional modules may fail. (e.g., using Docker or Kubernetes.)
- **Full Build:**
Builds all modules compatible with your system configuration. Recommended for production and development environments.

After installation, reboot the system following the instructions shown in the terminal.

### Manual Installation

If for some reason you want to build and install the FLASH kernel manually, follow the steps below.

#### Install dependencies: 

To build the kernel, you need to install few packages that are required:

```bash
sudo apt update
sudo apt install build-essential libncurses-dev bison flex libssl-dev libelf-dev fakeroot dwarves bc
```

#### Build the kernel:

```bash
git clone https://github.com/networkedsystemsIITB/flash-linux.git
cd flash-linux
make olddefconfig
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
scripts/config --set-str CONFIG_SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str CONFIG_SYSTEM_REVOCATION_KEYS ""
```

For advanced configuration, see the [Linux kernel build guide](https://www.kernel.org/doc/Documentation/admin-guide/README.rst).

Now build:

```bash
make -j$(nproc)
```

#### Install the kernel:

```bash
sudo make modules_install
sudo make install
```

After the kernel is installed, you just need to reboot the system and select the FLASH kernel from the GRUB menu:

```bash
sudo reboot
```

After rebooting, you can verify that the FLASH kernel is running by executing:

```bash
uname -r
```

## FLASH Kernel Features

> This section is intended for developers building network frameworks or libraries on top of the FLASH kernel.  
> Regular users should rely on the FLASH userspace library, which abstracts these details automatically.

### Sysfs Interface for AF_XDP Socket Management

FLASH kernel exposes a sysfs interface under `/sys/kernel/flash` allowing privileged users to:
- Inspect active AF_XDP sockets
- CConfigure redirection rules between sockets
- Adjust per-socket parameters (e.g., TX tracking)

Each AF_XDP socket is identified by a process-independent identifier called a flash-id, which enables cross-process management.

When a new AF_XDP socket is created, FLASH automatically registers it in sysfs under a dedicated directory: `/sys/kernel/flash/<flash-id>/`

Below is an example sysfs layout for three sockets with flash-ids 1, 2, and 3:

```bash
/sys/kernel/flash/
│
├── tx_tracking                # Global TX tracking control (0 or 1)
│
├── 1/
│   ├── pid                    # Process ID owning this socket
│   ├── procname               # Process name
│   ├── ifindex                # Network interface index
│   ├── qid                    # Queue ID
│   └── next                   # Redirection targets
│
├── 2/
│   └── ...
│
└── 3/
    └── ...
```

Socket directories are dynamically created when a socket is registered and automatically removed upon closure.

#### Configuring Redirections

The `next` file defines downstream paths for packet redirection.

- A value of -1 means the socket transmits packets directly to the NIC (no redirection).
- Writing one or more flash-ids to this file defines new downstream redirection targets.

**Examples:**

a. Redirect socket 1 → socket 2:

```console
# cat /sys/kernel/flash/1/next
-1
# echo 2 | sudo tee /sys/kernel/flash/1/next
# cat /sys/kernel/flash/1/next
index   flash_id
0       2
```

Redirect socket 1 → sockets 2 and 3:

```console
# cat /sys/kernel/flash/1/next
-1
# echo "2 3" | sudo tee /sys/kernel/flash/1/next
# cat /sys/kernel/flash/1/next
index   flash_id
0       2
1       3
```

Clear all redirections:

```console
# echo "-1" | sudo tee /sys/kernel/flash/1/next
# cat /sys/kernel/flash/1/next
-1
```

#### Runtime redirection semantics

When sending packets from an AF_XDP socket:

- Single target: All packets are forwarded to that target.
- Multiple targets: The lower 16 bits of the packet descriptor’s flags field determine the destination index (default = 0). [index is the value written in the `next` file]

The redirection automatically happens in zero-copy if the sockets share UMEM. If the sockets do not share UMEM, FLASH falls back to copying packets between sockets.

From user-space, this behavior is transparent; sending to redirected sockets behaves as with regular AF_XDP sockets.

### Using `poll()` for Interrupt vs. Busy-Polling Mode

AF_XDP sockets can operate in both interrupt-driven and busy-polling modes, allowing applications to balance CPU utilization and latency depending on workload characteristics.

Recommended workflow:

1. **Start in interrupt mode:** Use `poll()` with the `POLLIN` flag to block until packets arrive.
2. **Switch to busy-polling:** When packet rates are consistently high, switch to a busy loop using `recvfrom()` to continuously process packets. This eliminates interrupt latency and can improve throughput.
3. **Revert to interrupt mode:** When load decreases, return to interrupt mode to save CPU cycles.

FLASH ensures that `poll()` correctly reflects packet readiness even when redirection chains are configured, enabling seamless transitions between modes without losing events or packets.

### Backpressure Handling with `poll()` and `recvfrom()`

Backpressure arises when downstream sockets (receivers) cannot process packets as fast as they are being produced. The FLASH Kernel introduces natural backpressure into the AF_XDP data path via the TX and CQ rings: packets are not transmitted to downstream sockets if their RX rings are full.

In this scenario, the sender must retry transmissions untill space becomes available. To avoid wasting CPU cycles through busy-waiting, applications should rely on the following readiness mechanism:

- When congestion is detected on a sender socket, use `poll()` with the `POLLOUT` flag to sleep until the socket is ready to send again.
- Receiver sockets should use `recvfrom()` with the `MSG_MORE` flag to implicitly signal the sender once they have freed space.

The FLASH kernel ensure that the signal from the receiver propagates upstream through the redirection chain, waking up any blocked senders. This cooperative signaling model helps maintain steady throughput while preventing packet loss or excessive CPU usage under heavy load.

### TX Tracking per Flow (Mitigating Head-of-Line Blocking)

When multiple downstream sockets are configured for redirection, one slow or congested target can cause head-of-line (HOL) blocking, where faster flows are stalled by slower ones. To mitigate this, FLASH provides a global TX tracking mechanism, which can be enabled or disabled through the `tx_tracking` sysfs file.

```bash
echo 1 | sudo tee /sys/kernel/flash/tx_tracking   # Enable TX tracking
echo 0 | sudo tee /sys/kernel/flash/tx_tracking   # Disable TX tracking
```

When enabled, FLASH tracks packet transmission status on a per-flow basis.  
As packets are transmitted succesfully, the kernel writes back the `flash_id` of the downstream socket into the memory location specified by the packet descriptor before returning it to the completion queue. This allows user-space applications to identify which downstream path successfully transmitted each packet.

Applications can use this feedback to implement:
- Dynamic congestion control or flow rerouting,
- Per-destination pacing or fair queuing, and
- Custom recovery strategies to reduce the impact of HOL blocking.

> Note: TX tracking adds slight overhead due to per-flow bookkeeping. It is recommended for applications that require advanced flow management or fairness across multiple redirection targets.

## Uninstalling the FLASH Kernel

You can use the `uninstall.sh` script to remove the FLASH kernel and restore the previous kernel.

```bash
cd flash
sudo ./usertools/flash_kernel/uninstall.sh
```

The script will ask for kernel version to uninstall and will update the GRUB configuration accordingly. After uninstalling, reboot the system to boot into the previous kernel.

> Note: Make sure that the previous kernel is still installed on your system before uninstalling the FLASH kernel.
