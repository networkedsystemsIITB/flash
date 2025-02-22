# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

sources = files('flash_nf.c', 'flash_txrx.c')
headers = files('flash_nf.h')

deps += [uds, log]

libnf = library(libname, sources, install: true, dependencies: deps)
nf = declare_dependency(link_with: libnf, include_directories: include_directories('.'))

flash_libs += nf