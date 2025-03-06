# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

sources = files('flash_cfgparser.c')
headers = files('flash_cfgparser.h')

libcfgparser = library(libname, sources, install: true, dependencies: deps)
cfgparser = declare_dependency(link_with: libcfgparser, include_directories: include_directories('.'))

flash_libs += cfgparser