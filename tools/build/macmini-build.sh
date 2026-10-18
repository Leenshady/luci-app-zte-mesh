#!/bin/sh
set -e
export PATH=/usr/local/bin:/opt/homebrew/bin:$PATH
cd /Volumes/code/zx279133/port/mesh-shim
A="$PWD/../../out/mesh-shim"
rm -f "$A/db-engine-probe" "$A/zte-meshctl"
docker run --rm --platform linux/arm64 --network none --read-only --cap-drop ALL --security-opt no-new-privileges \
  --tmpfs /tmp:rw,nosuid,size=64m --pids-limit 64 --memory 512m -e PYTHONDONTWRITEBYTECODE=1 \
  -v "$PWD:/src:ro" -v "$A:/artifacts" sr1010-mesh-shim-builder make ARTIFACTS=/artifacts /artifacts/db-engine-probe /artifacts/zte-meshctl
