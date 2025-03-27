#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das.

# Setup mlx5 network interface for AF_XDP

set -e

usage() {
    echo "Usage: $0"
    echo "  -i, --interface <interface>        Network interface for setting up AF_XDP (mandatory)"
    echo "  -q, --queues <num-queues>          Number of queues (mandatory)"
    echo "  -m, --mtu <mtu>                    MTU value (default: 1500)"
    echo "  -p, --pci-max-read-req             Set PCI MaxReadReq to 1024(exclusive with -b)"
    echo "  -b, --busy-polling                 Enable Busy Polling (exclusive with -p)"
    echo "  -h, --hyper-threading              Enable Hyper-threading"
    echo "  -r, --real-time-throttling         Disable Real-time Throttling"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -i|--interface)
            NIC="$2"
            shift 2
            ;;
        -q|--queues)
            QUEUES="$2"
            shift 2
            ;;
        -m|--mtu)
            MTU="$2"
            shift 2
            ;;
        -p|--pci-max-read-req)
            PCI_MAX_READ_REQ=1
            shift
            ;;
        -b|--busy-polling)
            BUSY_POLLING=1
            shift
            ;;
        -h|--hyper-threading)
            HYPER_THREADING=1
            shift
            ;;
        -r|--real-time-throttling)
            REAL_TIME_THROTTLING="1"
            shift
            ;;
        *)
            usage
            ;;
    esac
done

# Set default values if not provided
NIC=${NIC:-}
QUEUES=${QUEUES:-}
MTU=${MTU:-1500}
PCI_MAX_READ_REQ=${PCI_MAX_READ_REQ:-0}
BUSY_POLLING=${BUSY_POLLING:-0}
HYPER_THREADING=${HYPER_THREADING:-0}
REAL_TIME_THROTTLING=${REAL_TIME_THROTTLING:-1}

if [ -z "$NIC" ] || [ -z "$QUEUES" ]; then
    echo "Please provide both network interface and number of queues."
    usage
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

if [ "$PCI_MAX_READ_REQ" = "1" ]; then
    r68w=`setpci -s $PCI 68.w`
    new_r68w="3${r68w:1}"
    echo "$PCI: Old 68.w=$r68w. New 68.w=$new_r68w"
    setpci -s $PCI 68.w=$new_r68w
fi

echo "Optimizing Virtual Memory Usage..."
sysctl -w vm.zone_reclaim_mode=0
sysctl -w vm.swappiness=0

bp=`cat /sys/class/net/$NIC/napi_defer_hard_irqs`
if [ "$BUSY_POLLING" = "1" ]; then
    if [ "$bp" != "2" ]; then
        echo 2 > /sys/class/net/$NIC/napi_defer_hard_irqs
        echo 200000 > /sys/class/net/$NIC/gro_flush_timeout
    fi
else
    if [ "$bp" = "2" ]; then
        echo 0 > /sys/class/net/$NIC/napi_defer_hard_irqs
        echo 0 > /sys/class/net/$NIC/gro_flush_timeout
    fi
fi

ht=`cat /sys/devices/system/cpu/smt/active`
if [ "$HYPER_THREADING" = "0" ]; then
    if [ "$ht" = "1" ]; then
        echo off > /sys/devices/system/cpu/smt/control
    fi
else
    if [ "$ht" = "0" ]; then
        echo on > /sys/devices/system/cpu/smt/control
    fi
fi

if [ "$REAL_TIME_THROTTLING" = "1" ]; then
    echo "Disabling Real-time Throttling..."
    echo -1 > /proc/sys/kernel/sched_rt_runtime_us
fi
