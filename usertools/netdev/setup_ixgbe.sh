#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das.
#
# Setup ixgbe network interface for AF_XDP

set -e

if [ "$1" = "" ]; then
    echo "Provide the network interface for setting up AF_XDP"
    echo "Usage: $0 <interface> [num-queues]"
    echo "[num-queues] is optional"
    echo "Example: $0 enp1s0f0 2"
    exit 1
fi

NIC=$1

QUEUES=1
if [[ "$2" != "" ]]; then
    QUEUES=$2
fi

PCI=`ethtool -i $NIC | grep 'bus-info:' | sed 's/bus-info: //'`

echo "Setting ethtool-based optimizations..."
ethtool -A $NIC tx off rx off
ethtool -L $NIC combined $QUEUES
ethtool -X $NIC equal 1
ip link set dev $NIC up

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
