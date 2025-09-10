# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

sources = files('log.c')
headers = files('log.h')

liblog = library(libname, sources, install: true)
log = declare_dependency(link_with: liblog, include_directories: include_directories('.'))

flash_libs += log