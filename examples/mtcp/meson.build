# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

sources = files('epserver.c')

deps += [mtcp, util]

executable('epserver', sources, c_args: cflags, install: true, dependencies: deps)
