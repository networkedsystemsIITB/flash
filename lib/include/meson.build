# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

sources = []
headers = files('flash_defines.h', 'flash_list.h')

include = declare_dependency(include_directories: include_directories('.'))

install_headers(headers, subdir: meson.project_name().to_lower())