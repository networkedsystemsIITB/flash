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

uschain = files('backpressure.c')
executable('backpressure', uschain, c_args: cflags, install: true, dependencies: deps)