FROM ubuntu:16.04

RUN \
    apt-get update && \
    apt-get install -y \
    clang curl git libgnutls30 libgnutls-dev \
    libcurl4-gnutls-dev libnuma-dev libpcap-dev libsctp-dev

RUN \
    curl https://sh.rustup.rs -sSf | \
    sh -s -- -y --default-toolchain nightly-2019-01-19

ENV PATH="/root/.cargo/bin:${PATH}"

ARG OLD_CONFIG='DPDK_CONFIG_FILE=\${DPDK_CONFIG_FILE-"\${EXT_BASE}\/dpdk-confs\/common_linuxapp-\${DPDK_VER}"}'
ARG NEW_CONFIG='DPDK_CONFIG_FILE="\${EXT_BASE}\/dpdk-confs\/common_linuxapp-\${DPDK_VER}.container"'

RUN git clone https://github.com/hsadok/NetBricks.git

WORKDIR /NetBricks

RUN sed -i "s|${OLD_CONFIG}|${NEW_CONFIG}|" build.sh && ./build.sh
