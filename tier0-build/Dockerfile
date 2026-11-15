FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash \
        bc \
        bison \
        build-essential \
        ca-certificates \
        ccache \
        cpio \
        file \
        flex \
        git \
        gawk \
        gzip \
        libncurses-dev \
        locales \
        make \
        patch \
        python3 \
        python3-pexpect \
        rsync \
        sed \
        tar \
        unzip \
        wget \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

# Buildroot refuses to run as root (host packages abort their configure).
# /work/cache is a named volume (Buildroot, downloads, ccache, outputs): a
# new volume copies this directory's ownership, so builder can write to it.
RUN useradd -m -s /bin/bash builder \
    && mkdir -p /work/cache /work/blackhand \
    && chown -R builder:builder /work
USER builder
WORKDIR /work/blackhand
