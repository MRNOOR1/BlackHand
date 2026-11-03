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
# Build as an unprivileged user; /work is writable by it.
RUN useradd -m -s /bin/bash builder \
    && mkdir -p /work \
    && chown builder:builder /work
USER builder

WORKDIR /work

ENTRYPOINT ["bash", "-lc", "BUILDROOT_REPO=${BUILDROOT_REPO:-https://github.com/buildroot/buildroot.git}; BUILDROOT_BRANCH=${BUILDROOT_BRANCH:-2024.02.x}; BUILDROOT_DIR=${BUILDROOT_DIR:-/work/buildroot}; if [[ ! -d \"${BUILDROOT_DIR}\" ]]; then git clone --depth 1 --branch \"${BUILDROOT_BRANCH}\" \"${BUILDROOT_REPO}\" \"${BUILDROOT_DIR}\"; fi; cd \"${BUILDROOT_DIR}\"; exec bash"]
