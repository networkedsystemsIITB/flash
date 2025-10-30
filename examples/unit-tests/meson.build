# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

correctness = files('correctness.c')
executable('correctness', correctness, c_args: cflags, install: true, dependencies: deps)

fwd_drop = files('fwddrop.c')
executable('fwddrop', fwd_drop, c_args: cflags, install: true, dependencies: deps)

fwd_rr = files('fwdrr.c')
executable('fwdrr', fwd_rr, c_args: cflags, install: true, dependencies: deps)

uschain = files('userspace-chain.c')
executable('userspace-chain', uschain, c_args: cflags, install: true, dependencies: deps)

ring_benchmark = files('ring-benchmark.c')
executable('ring-benchmark', ring_benchmark, c_args: cflags, install: true, dependencies: deps)

backpressure = files('backpressure.c')
executable('backpressure', backpressure, c_args: cflags, install: true, dependencies: deps)

multi_flow_tx = files('multi-flow-tx.c')
executable('multi-flow-tx', multi_flow_tx, c_args: cflags, install: true, dependencies: deps)