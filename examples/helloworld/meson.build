# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

sources = files('main.c')

deps += [log, nf, params]

executable('helloworld',
    sources,
    c_args: cflags,
    install: true,
    dependencies: deps)