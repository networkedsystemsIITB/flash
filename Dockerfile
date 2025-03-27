ARG BASE_IMAGE=ubuntu:latest
ARG DEBIAN_FRONTEND=noninteractive

FROM ${BASE_IMAGE} AS build

ARG DEBIAN_FRONTEND

RUN \
    apt-get update && \
    apt-get install -y \
    build-essential \
    libbpf-dev \
    pkg-config

FROM build AS xdp

ARG DEBIAN_FRONTEND

RUN \
    apt-get install -y \
    git \
    clang \
    llvm \
    lld \
    m4 \
    libpcap-dev && \
    rm -rf /var/lib/apt/lists/*

RUN \
    git clone https://github.com/xdp-project/xdp-tools.git && \
    make -j -C xdp-tools libxdp && \
    make -j -C xdp-tools libxdp_install

FROM build AS flash-build

ARG DEBIAN_FRONTEND

WORKDIR /flash

RUN \
    apt-get install -y \
    meson \
    gcc-multilib \
    libcjson-dev \
    libncurses-dev && \
    rm -rf /var/lib/apt/lists/*

COPY --from=xdp /usr/local/include/xdp /usr/local/include/xdp
COPY --from=xdp /usr/local/lib/pkgconfig/libxdp.pc /usr/local/lib/pkgconfig/
COPY --from=xdp /usr/local/lib/bpf /usr/local/lib/bpf
COPY --from=xdp /usr/local/lib/libxdp.* /usr/local/lib/
COPY --from=xdp /usr/include/linux/if_xdp.h /usr/include/linux/
COPY --from=xdp /usr/include/linux/xdp_diag.h /usr/include/linux/

COPY . .

RUN make

FROM flash-build

RUN useradd -d /flash flash

USER flash
