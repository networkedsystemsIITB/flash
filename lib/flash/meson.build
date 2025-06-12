# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

special_dirs = [
    'log',
]

dirs = ['uds', 'common', 'monitor', 'params', 'pool', 'nf']

foreach special_dir : special_dirs
    sources = []
    headers = []
    libname = 'flash_' + special_dir
    enabled_libs += libname

    subdir(special_dir)

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