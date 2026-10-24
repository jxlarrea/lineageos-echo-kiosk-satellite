FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive \
    USER=builder \
    LC_ALL=C

# The package list from the LineageOS 18.1 build guide, plus what the
# MT8163 4.9 kernel build needs (cpio, kmod, libelf-dev).
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    bc \
    bison \
    build-essential \
    ca-certificates \
    ccache \
    cpio \
    curl \
    flex \
    g++-multilib \
    gcc-multilib \
    git \
    gnupg \
    gperf \
    imagemagick \
    kmod \
    lib32ncurses5-dev \
    lib32readline-dev \
    lib32z1-dev \
    libc6-dev-i386 \
    libelf-dev \
    liblz4-tool \
    libncurses5 \
    libncurses5-dev \
    libsdl1.2-dev \
    libssl-dev \
    libxml2 \
    libxml2-utils \
    lzop \
    pngcrush \
    python-is-python3 \
    python3 \
    rsync \
    schedtool \
    squashfs-tools \
    unzip \
    xsltproc \
    xxd \
    zip \
    zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

RUN useradd -m -u 1000 builder
USER builder
WORKDIR /src
