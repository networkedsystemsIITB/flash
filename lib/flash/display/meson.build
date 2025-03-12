# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

sources = files('flash_display.c')
headers = files('flash_display.h')

deps += [monitor]

libdisplay = library(libname, sources, install: true, dependencies: deps)
display = declare_dependency(link_with: libdisplay, include_directories: include_directories('.'))

flash_libs += libdisplay