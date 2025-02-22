# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

sources = files('flash_params.c')
headers = files('flash_params.h')

deps += [log]

libparams = library(libname, sources, install: true, dependencies: deps)
params = declare_dependency(link_with: libparams, include_directories: include_directories('.'))

flash_libs += params