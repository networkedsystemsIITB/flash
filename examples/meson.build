# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

dirs = [
    'helloworld',
    'l2fwd',
]

def_deps = [include, log, nf, params, uds]

foreach d : dirs
    cflags = []
    deps = def_deps # reset the deps to the original values

    sources = []
    headers = []

    subdir(d)

    install_headers(headers, subdir: meson.project_name().to_lower())
endforeach