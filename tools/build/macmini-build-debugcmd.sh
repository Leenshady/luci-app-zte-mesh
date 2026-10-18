#!/bin/sh
set -e
export PATH=/usr/local/bin:/opt/homebrew/bin:$PATH
cd /Volumes/code/zx279133/port/mesh-shim
A="$PWD/../../out/mesh-shim"
rm -f "$A/zte-debugcmd"
docker run --rm --platform linux/arm64 --network none --read-only --cap-drop ALL --security-opt no-new-privileges \
  --tmpfs /tmp:rw,nosuid,size=64m --pids-limit 64 --memory 512m -v "$PWD:/src:ro" -v "$A:/artifacts" sr1010-mesh-shim-builder \
  arm-linux-gnueabi-gcc -O2 -std=gnu11 -Wall -Wextra -Werror -march=armv7-a -mfloat-abi=soft -static /src/zte-debugcmd.c -o /artifacts/zte-debugcmd
