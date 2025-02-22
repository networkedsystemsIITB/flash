# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

special_dirs = [
    'log',
]

dirs = [
    'params',
    'uds',
    'monitor',
    'nf',
]

foreach d : special_dirs
    sources = []
    headers = []
    libname = 'flash_' + d
    enabled_libs += libname

    subdir(d)

    install_headers(headers, subdir: meson.project_name().to_lower())
endforeach

foreach dir : dirs
    deps = [include, log]

    sources = []
    headers = []
    libname = 'flash_' + dir
    enabled_libs += libname

    subdir(dir)

    install_headers(headers, subdir: meson.project_name().to_lower())
endforeach