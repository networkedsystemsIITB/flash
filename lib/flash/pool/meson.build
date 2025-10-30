# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

sources = files('flash_pool.c')
headers = files('flash_pool.h')

deps += []

libpool = library(libname, sources, install: true, dependencies: deps)
pool = declare_dependency(link_with: libpool, include_directories: include_directories('.'))

flash_libs += pool