# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

sources = files('flash_uds.c')
headers = files('flash_uds.h')

libuds = library(libname, sources, install: true, dependencies: deps)
uds = declare_dependency(link_with: libuds, include_directories: include_directories('.'))

flash_libs += uds