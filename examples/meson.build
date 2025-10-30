# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

# C examples
dirs = [
    'unit-tests',
    'helloworld',
    'simplefwd',
    'l2fwd',
    'ip4ping',
    'maglev',
    'simple-firewall',
    'firewall',
    'arpresolver',
    'mica',
    'txgen'
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

if get_option('enable_rust') and cargo.found()
    message('>>> Configuring Rust examples')
    
    cargo_build_args = ['build', '--target-dir', rust_target_dir]
    
    if get_option('buildtype') == 'release'
        cargo_build_args += ['--release']
        message('Rust: Building in release mode')
    else
        message('Rust: Building in debug mode')
    endif
    
    if get_option('buildtype') == 'debug' or get_option('buildtype') == 'debugoptimized'
        cargo_build_args += ['-F', 'tracing']
    endif

    if get_option('enable_rust_stats')
        cargo_build_args += ['-F', 'stats']
        message('Rust: Enabling stats dashboard feature')
    endif
    
    rust_build = custom_target(
        'rust_workspace',
        output: 'rust_build_complete',
        command: [cargo] + cargo_build_args,
        console: true,
        build_by_default: true,
        build_always_stale: true,
    )
    
    message('<<< Rust examples configured')
endif