# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

sources = files('flash_cfgparser.c', 'flash_display.c', 'flash_monitor.c')
headers = files('flash_monitor.h')

deps += [uds, common]

libmonitor = library(libname, sources, install: true, dependencies: deps)
monitor = declare_dependency(link_with: libmonitor, include_directories: include_directories('.'))

flash_libs += monitor