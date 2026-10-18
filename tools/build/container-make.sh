#!/bin/sh
# Run make targets in the offline builder container (on the mac; Docker/OrbStack must be running).
# Products go to out/. Targets are artifact paths relative to out/ (db-engine-probe, ns-run,
# runtime/lib/libzte_controller_trace.so, ...) or the phony all, database, controller, test-host.
# An artifact is deleted INSIDE the container first: deleting it on the mac leaves a stale
# virtiofs view that makes make think the old file is up to date.
#
# usage: tools/build/container-make.sh target...
set -eu
[ $# -gt 0 ] || { echo "usage: $0 target..." >&2; exit 2; }
export PATH=/usr/local/bin:/opt/homebrew/bin:$PATH
REPO=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${OUT:-"$REPO/out"}
IMAGE=${IMAGE:-sr1010-mesh-shim-builder}
rm_list=
make_list=
for t in "$@"; do
	case "$t" in
	all|database|controller|test-host) make_list="$make_list $t" ;;
	*) rm_list="$rm_list /artifacts/$t"; make_list="$make_list /artifacts/$t" ;;
	esac
done
mkdir -p "$OUT"
exec docker run --rm --platform linux/arm64 --network none --read-only --cap-drop ALL \
	--security-opt no-new-privileges --tmpfs /tmp:rw,nosuid,size=64m --pids-limit 64 --memory 512m \
	-e PYTHONDONTWRITEBYTECODE=1 -v "$REPO:/src:ro" -v "$OUT:/artifacts" "$IMAGE" \
	sh -c "rm -f $rm_list && make ARTIFACTS=/artifacts $make_list"
