# Multi-stage build for optimized image
FROM ubuntu:22.04 AS base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc g++ \
    make \
    git \
    rsync \
    bc \
    bison flex \
    gawk \
    patch \
    unzip \
    file \
    wget \
    cpio \
    python3 \
    python3-pip \
    libncurses5-dev \
    libssl-dev \
    libelf-dev \
    device-tree-compiler \
    xz-utils \
    ca-certificates \
    qemu-user-static \
    && rm -rf /var/lib/apt/lists/*

FROM base AS builder

RUN groupadd -g 1000 builder && useradd -m -u 1000 -g builder builder

USER builder
WORKDIR /home/builder
