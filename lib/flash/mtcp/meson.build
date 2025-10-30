# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

sources = files('core.c',
  'tcp_stream.c',
  'config.c',
  'api.c',
  'eventpoll.c',
  'socket.c',
  'pipe.c',
  'tcp_util.c',
  'eth_in.c',
  'ip_in.c',
  'tcp_in.c',
  'eth_out.c',
  'ip_out.c',
  'tcp_out.c',
  'arp.c',
  'timer.c',
  'cpu.c',
  'rss.c',
  'addr_pool.c',
  'fhash.c',
  'memory_mgt.c',
  'logger.c',
  'debug.c',
  'tcp_rb_frag_queue.c',
  'tcp_ring_buffer.c',
  'tcp_send_buffer.c',
  'tcp_sb_queue.c',
  'tcp_stream_queue.c',
  'psio_module.c',
  'io_module.c',
  'dpdk_module.c',
  'onvm_module.c',
  'icmp.c',
  'flash_module.c')

headers = files('include/mtcp_api.h', 'include/mtcp_epoll.h')

deps += [include, log, nf, params, uds]

libmtcp = library(libname, sources, install: true, dependencies: deps, include_directories: include_directories('./include'))
mtcp = declare_dependency(link_with: libmtcp, include_directories: include_directories('./include'))

flash_libs += mtcp
