#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das
# For CI use only

SUDO=
if [ $(id -u) -ne 0 ]; then
    SUDO=sudo
fi

$SUDO apt install -y build-essential meson libbpf-dev pkg-config git gcc-multilib clang llvm lld m4 libpcap-dev libcjson-dev libncurses-dev

git clone https://github.com/xdp-project/xdp-tools.git
make -C xdp-tools
$SUDO make -C xdp-tools install

meson setup build
meson compile -C build
