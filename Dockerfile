FROM debian:bookworm-slim
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    gcc gcc-arm-linux-gnueabi binutils-arm-linux-gnueabi libc6-dev libc6-dev-armel-cross make \
    python3 qemu-user-static \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
