# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

dirs = [
    'helloworld',
    'l2fwd',
    'fwddrop',
    'maglev',
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