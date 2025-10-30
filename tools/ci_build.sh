#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das
# For CI use only

SUDO=
if [ $(id -u) -ne 0 ]; then
    SUDO=sudo
fi

$SUDO apt update
$SUDO apt install -y build-essential meson libbpf-dev pkg-config git gcc-multilib clang llvm lld m4 libpcap-dev libcjson-dev libncurses-dev libnuma-dev

git clone https://github.com/xdp-project/xdp-tools.git
make -j -C xdp-tools libxdp
$SUDO make -j -C xdp-tools libxdp_install
make PREFIX=/usr -j -C xdp-tools libxdp
$SUDO PREFIX=/usr make -j -C xdp-tools libxdp_install

curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

meson setup build
meson compile -C build
