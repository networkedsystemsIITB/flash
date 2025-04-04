# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

sources = files('main.c', 'ported-mica/alloc_dynamic.c', 'ported-mica/alloc_malloc.c', 'ported-mica/alloc_pool.c', 'ported-mica/city.c', 'ported-mica/hash.c', 'ported-mica/shm.c', 'ported-mica/table.c', 'ported-mica/util.c')

executable('mica', sources, c_args: cflags, install: true, dependencies: deps)