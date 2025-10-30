# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

sources = files(
  'tdate_parse.c',
  'http_parsing.c',
  'netlib.c',
  'cpu.c'
)

headers = files(
  'include/tdate_parse.h',
  'include/http_parsing.h',
  'include/netlib.h',
  'include/cpu.h'
)

deps += []

libutil = library(libname, sources, install: true, dependencies: deps, include_directories: include_directories('./include'))
util = declare_dependency(link_with: libutil, include_directories: include_directories('./include'))

flash_libs += util