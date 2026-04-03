FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bc \
    bison \
    build-essential \
    ca-certificates \
    cpio \
    flex \
    gcc-aarch64-linux-gnu \
    libelf-dev \
    libssl-dev \
    make \
    python3 \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*
