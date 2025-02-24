#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das.
#
# Setup mlx5 network interface for AF_XDP

set -e

if [ "$1" = "" ]; then
    echo "Provide the network interface for setting up AF_XDP"
    echo "Usage: $0 <interface> [num-queues] [mtu]"
    echo "[num-queues], [mtu] is optional"
    echo "Example: $0 enp1s0f0 2 3498"
    exit 1
fi

NIC=$1

QUEUES=1
if [[ "$2" != "" ]]; then
    QUEUES=$2
fi

MTU=1500
if [[ "$3" != "" ]]; then
    MTU=$3
fi

PCI=`ethtool -i $NIC | grep 'bus-info:' | sed 's/bus-info: //'`



echo "Setting MTU to $MTU..."
ip link set dev $NIC mtu $MTU


echo "Setting ethtool-based optimizations..."
ethtool -A $NIC tx off rx off
ethtool -L $NIC combined $QUEUES
ethtool -C $NIC adaptive-rx off adaptive-tx off rx-usecs 0 tx-usecs 0
ethtool -K $NIC gro off rx-fcs off sg off tx-ipxip4-segmentation off rx-checksumming off tx-checksumming off \
                tx-udp-segmentation off gso off rx-gro-list off tso off tx-ipxip6-segmentation off \
                tx-udp_tnl-csum-segmentation off hw-tc-offload off rx-vlan-stag-filter off \
                rx-udp-gro-forwarding off tx off tx-nocache-copy off tx-udp_tnl-segmentation off \
                lro off rx-udp_tunnel-port-offload off tx-checksum-ip-generic off \
                tx-scatter-gather off tx-vlan-stag-hw-insert off ntuple on rx-vlan-filter off \
                tx-gre-csum-segmentation off tx-tcp-mangleid-segmentation off txvlan off rx off \
                rxhash on tx-gre-segmentation off tx-tcp-segmentation off rx-all off rxvlan off \
                tx-gso-partial off tx-tcp6-segmentation off rx-checksumming off tx-checksumming off
                
ethtool --set-priv-flags $NIC rx_cqe_compress on


read -p "Set PCI MaxReadReq to 1024? [y/n]..." -n 1 answer
echo ""
if [ "$answer" = "y" ]; then
    # https://enterprise-support.nvidia.com/s/article/understanding-pcie-configuration-for-maximum-performance
    r68w=`setpci -s $PCI 68.w`
    new_r68w="3${r68w:1}"
    echo "$pci: Old 68.w=$r68w. New 68.w=$new_r68w"
    setpci -s $PCI 68.w=$new_r68w
fi

echo "Optimizing Virtual Memory Usage..."
sysctl -w vm.zone_reclaim_mode=0
sysctl -w vm.swappiness=0

bp=`cat /sys/class/net/$NIC/napi_defer_hard_irqs`
if [ "$bp" = "2" ]; then
    echo "Busy Polling is enabled!"
    read -p "Disable Busy Polling? [y/n]..." -n 1 answer
    echo ""
    if [ "$answer" = "y" ]; then
        echo 0 > /sys/class/net/$NIC/napi_defer_hard_irqs
        echo 0 > /sys/class/net/$NIC/gro_flush_timeout
    fi
else
    echo "Busy Polling is disabled!"
    read -p "Enable Busy Polling? [y/n]..." -n 1 answer
    echo ""
    if [ "$answer" = "y" ]; then
        echo 2 > /sys/class/net/$NIC/napi_defer_hard_irqs
        echo 200000 > /sys/class/net/$NIC/gro_flush_timeout
    fi
fi

ht=`cat /sys/devices/system/cpu/smt/active`
if [ "$ht" = "1" ]; then
    echo "Hyper-threading is enabled!"
    read -p "Disable Hyperthreading? [y/n]..." -n 1 answer
    echo ""
    if [ "$answer" = "y" ]; then
        echo off > /sys/devices/system/cpu/smt/control
    fi
else
    echo "Hyper-threading is disabled!"
fi

echo "Disabling Real-time Throttling..."
echo -1 > /proc/sys/kernel/sched_rt_runtime_us
