# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

sources = files('log.c')
headers = files('log.h')

log_c_args = []
if get_option('log_use_color')
    log_c_args += ['-DLOG_USE_COLOR']
endif

if get_option('buildtype') == 'debug' or get_option('buildtype') == 'debugoptimized'
    log_c_args += ['-DLOG_ENABLE_DEBUG']
    message('Log library: Debug/trace logging enabled for build type: ' + get_option('buildtype'))
else
    message('Log library: Debug/trace logging disabled for build type: ' + get_option('buildtype'))
endif

liblog = library(libname, sources, 
                 install: true,
                 c_args: log_c_args)
log = declare_dependency(link_with: liblog, include_directories: include_directories('.'))

flash_libs += log