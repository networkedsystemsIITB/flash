# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

sources = files('flash_nf.c', 'flash_stats.c', 'flash_txrx.c', 'flash_txrx_dev.c')
headers = files('flash_nf.h')

deps += [uds, common]

libnf = library(libname, sources, install: true, dependencies: deps)
nf = declare_dependency(link_with: libnf, include_directories: include_directories('.'))

flash_libs += nf