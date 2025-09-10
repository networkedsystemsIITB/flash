# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

project(
    'flash',
    'C',
    version: '25.9-beta',
    license: 'Apache-2.0',
    default_options: [
        'buildtype=release',
        'default_library=shared',
        'warning_level=3',
        'werror=true',
    ],
    meson_version: '>= 0.53.2',
)

use_static_libs = get_option('default_library') == 'static'

cc = meson.get_compiler('c')

# Enable Rust support if the option is set and Cargo is available
enable_rust = get_option('enable_rust')
cargo = find_program('cargo', required: enable_rust)
rust_build = []
rust_target_dir = ''
rust_profile = ''

if enable_rust and cargo.found()
    message('Rust support enabled')
    rust_target_dir = meson.current_build_dir() / 'rust-target'
else
    message('Rust support disabled')
endif

build_dir = meson.current_build_dir()
flash_libs = []
enabled_libs = []

# to pass comma separated values to the linker
add_project_link_arguments('-Wl,--no-as-needed', language: 'c')

# use pthreads library
add_project_link_arguments('-pthread', language: 'c')

# use math library
add_project_link_arguments('-lm', language: 'c')

# use cjson library
if cc.has_header('cjson/cJSON.h', required: true)
    add_project_link_arguments('-lcjson', language: 'c')
endif

# use ncurses library
if cc.has_header('ncurses.h', required: true)
    add_project_link_arguments('-lncurses', language: 'c')
endif

# use libnuma library
if cc.has_header('numa.h', required: true)
    add_project_link_arguments('-lnuma', language: 'c')
endif

add_project_link_arguments('-ldl', language: 'c')

xdp_dep = dependency(
    'libxdp',
    version: '>=1.2.0',
    required: true,
    method: 'pkg-config',
    static: use_static_libs,
)
bpf_dep = dependency(
    'libbpf',
    required: false,
    method: 'pkg-config',
    static: use_static_libs,
)
if not bpf_dep.found()
    bpf_dep = cc.find_library('bpf', required: true)
endif

if cc.has_header('linux/if_xdp.h')
    if xdp_dep.found() and cc.has_header('xdp/xsk.h')
        if bpf_dep.found() and cc.has_header('bpf/bpf.h')
            add_project_link_arguments('-lbpf', language: 'c')
            add_project_link_arguments('-lxdp', language: 'c')
        endif
    endif
endif

# Check whether the compiler and linker support User Interrupts. Some compilers have the -muintr
# flag, but the assembler can fail if it does not support the instructions. Check this by testing
# whether a simple program compiles and links.
tp = '#include <x86gprintrin.h>\nint main() { _stui(); return 0; }'
if cc.get_id() == 'gcc' and cc.links(tp, args: '-muintr', name: '-muintr')
    add_project_arguments('-muintr', language: 'c')
endif

# for clang 32-bit compiles we need libatomic for 64-bit atomic ops
if cc.get_id() == 'clang'
    atomic_dep = dependency(
        'atomic',
        required: false,
        method: 'pkg-config',
        static: use_static_libs,
    )
    if not atomic_dep.found()
        atomic_dep = cc.find_library('atomic', required: true)
    endif
    add_project_link_arguments('-latomic', language: 'c')
endif

# enable extra warnings and disable any unwanted warnings
warning_flags = [
    # additional warnings in alphabetical order
    '-Wno-pedantic',
    '-Wcast-qual',
    '-Wdeprecated',
    '-Wformat-nonliteral',
    '-Wformat-security',
    '-Wmissing-declarations',
    '-Wmissing-prototypes',
    '-Wnested-externs',
    '-Wold-style-definition',
    '-Wsign-compare',
    '-Wstrict-prototypes',
    '-Wundef',
    '-Wwrite-strings',

    # globally disabled warnings
    '-Wno-address-of-packed-member',
    '-Wno-packed-not-aligned',
    '-Wno-missing-field-initializers',
]
foreach arg : warning_flags
    if cc.has_argument(arg)
        add_project_arguments(arg, language: 'c')
    endif
endforeach

# Enable mtcp if the option is set
if get_option('enable_mtcp')
    mtcp_flag = [
    '-DDISABLE_ONVM',
    '-DDISABLE_PSIO',
    '-DDISABLE_DPDK',
    '-DDISABLE_NETMAP',
    '-fgnu89-inline',
    '-DNDEBUG',
    '-DNETSTAT',
    '-DINFO',
    '-DDBGERR',
    '-DDBGCERR',
    '-D__USRLIB__',
    ]

    foreach arg : mtcp_flag
        add_project_arguments(arg, language: 'c')
    endforeach
endif

# set loggging options for log library
if get_option('log_use_color')
    add_project_arguments('-DLOG_USE_COLOR', language: 'c')
endif

if get_option('buildtype') == 'debug' or get_option('buildtype') == 'debugoptimized'
    add_project_arguments('-DLOG_ENABLE_DEBUG', language: 'c')
    message('Log library: debug/trace logging enabled for build type - ' + get_option('buildtype'))
else
    message('Log library: debug/trace logging disabled for build type - ' + get_option('buildtype'))
endif

# specify -D_GNU_SOURCE unconditionally
add_project_arguments('-D_GNU_SOURCE', language: 'c')

##################################################################

subdir('lib')
subdir('monitor')
subdir('examples')

libflash_a = []
libflash_so = []
foreach lib : enabled_libs
    libflash_a += 'lib' + lib + '.a '
    libflash_so += 'lib' + lib + '.so '
endforeach

flash_a_name = 'libflash.a'
flash_so_name = 'libflash.so'

mklib = find_program('tools/mklib.sh')

if use_static_libs
    custom_target(
        'libflash_a_target',
        output: 'libflash.a',
        command: [mklib, build_dir, '@OUTPUT@', libflash_a],
        install_dir: join_paths('lib', 'x86_64-linux-gnu'),
        install: true,
    )
else
    custom_target(
        'libflash_so_target',
        output: 'libflash.so',
        command: [mklib, build_dir, '@OUTPUT@', libflash_so],
        install_dir: join_paths('lib', 'x86_64-linux-gnu'),
        install: true,
    )
endif

pkg = import('pkgconfig')

message('>>> Create pkg-config file')
pkg.generate(
    name: meson.project_name(),
    filebase: 'lib' + meson.project_name().to_lower(),
    version: meson.project_version(),
    libraries: ['-Wl,--as-needed'] + flash_libs,
    description: '''FLASH userspace library for AF_XDP userspace applications''',
    install_dir: 'lib/pkgconfig',
)
message('<<< Done pkg-config file')
