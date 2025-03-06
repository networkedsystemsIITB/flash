# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

sources = files('flash_common.c')
headers = files('flash_common.h')

libcommon = library(libname, sources, install: true, dependencies: deps)
common = declare_dependency(link_with: libcommon, include_directories: include_directories('.'))

flash_libs += common