#!/bin/sh
# Host test for the "Decide runtime deps for this target" step in build-apk.yml, without a router or an SDK.
# The step trims runtime deps out of DEPENDS (so they never enter the build graph) and must move every trimmed
# dep into EXTRA_DEPENDS (so the apk still declares it). This extracts the step's own shell from the YAML and
# runs it against a copy of the package Makefile for all four switch combinations, then reads the two lines back.
#
#   sh tests/test_build_deps.sh [WORKFLOW] [MAKEFILE]
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WF=${1:-$ROOT/.github/workflows/build-apk.yml}
MK_SRC=${2:-$ROOT/openwrt/zte-mesh/Makefile}
[ -f "$WF" ] || { echo "workflow not found: $WF" >&2; exit 1; }
[ -f "$MK_SRC" ] || { echo "Makefile not found: $MK_SRC" >&2; exit 1; }
T=$(mktemp -d) || exit 1
T=$(cd "$T" && pwd)
trap 'rm -rf "$T"' EXIT

# Pull the step's run block out of the YAML: everything between its "- name:" and the next one, with the
# 10-space block-scalar indent removed. Testing the extracted text (not a re-implementation) is the point --
# a sed that stops writing EXTRA_DEPENDS, or writes it unversioned, has to fail here.
awk '
  /^      - name: Decide runtime deps for this target$/ { grab = 1; next }
  grab && /^      - name: / { exit }
  grab && /^        run: \|$/ { inrun = 1; next }
  grab && inrun { sub(/^          /, ""); print }
' "$WF" > "$T/step.sh"
[ -s "$T/step.sh" ] || { echo "could not extract the runtime-deps step from $WF" >&2; exit 1; }

pass=0
fail=0
check() { # NAME EXPECTED ACTUAL
	if [ "$2" = "$3" ]; then pass=$((pass + 1)); else
		echo "FAIL $1: expected '$2', got '$3'"
		fail=$((fail + 1))
	fi
}

# run_case TRIM KMOD -- lay out the relative paths the step expects, run it, echo nothing; sets CASE_RC.
run_case() {
	rm -rf "$T/tree"
	mkdir -p "$T/tree/sr1010/immortalwrt" "$T/tree/zte-mesh/openwrt/zte-mesh"
	cp "$MK_SRC" "$T/tree/zte-mesh/openwrt/zte-mesh/Makefile"
	CASE_RC=0
	( cd "$T/tree/sr1010/immortalwrt" &&
	  TRIM_IP_FULL="$1" TARGET_BUILDS_KERNEL_MODULES="$2" sh "$T/step.sh" ) >"$T/out" 2>&1 || CASE_RC=$?
	MK="$T/tree/zte-mesh/openwrt/zte-mesh/Makefile"
}

dep_line() { sed -n 's/^[[:space:]]*DEPENDS:=[[:space:]]*//p' "$MK" | head -n 1; }
extra_line() { sed -n 's/^[[:space:]]*EXTRA_DEPENDS:=//p' "$MK" | head -n 1; }
has_extra() { grep -qE '^[[:space:]]*EXTRA_DEPENDS:=' "$MK" && echo yes || echo no; }

# Default target: everything trimmed out of the graph, all three kept as runtime deps.
run_case true false
check "default rc" 0 "$CASE_RC"
check "default DEPENDS empty" "" "$(dep_line)"
check "default EXTRA present" yes "$(has_extra)"
check "default EXTRA value" "ip-full (>=0), kmod-veth (>=0), kmod-sched-core (>=0)" "$(extra_line)"

# In-tree kernel: the kmods stay in DEPENDS, only ip-full moves to EXTRA_DEPENDS.
run_case true true
check "kmod-build rc" 0 "$CASE_RC"
check "kmod-build DEPENDS" "+kmod-veth +kmod-sched-core" "$(dep_line)"
check "kmod-build EXTRA value" "ip-full (>=0)" "$(extra_line)"

# ip-full kept, kmods trimmed: only the kmods move to EXTRA_DEPENDS.
run_case false false
check "ip-kept rc" 0 "$CASE_RC"
check "ip-kept DEPENDS" "+ip-full" "$(dep_line)"
check "ip-kept EXTRA value" "kmod-veth (>=0), kmod-sched-core (>=0)" "$(extra_line)"

# Nothing trimmed: DEPENDS untouched and no EXTRA_DEPENDS line at all.
run_case false true
check "untouched rc" 0 "$CASE_RC"
check "untouched DEPENDS" "+ip-full +kmod-veth +kmod-sched-core" "$(dep_line)"
check "untouched EXTRA absent" no "$(has_extra)"

# The in-tree guard still fires: a Makefile that lost +kmod-veth must not pass when kmods are kept.
run_case false true
sed -i -E 's/[[:space:]]*\+kmod-veth//g' "$MK"
CASE_RC=0
( cd "$T/tree/sr1010/immortalwrt" &&
  TRIM_IP_FULL=false TARGET_BUILDS_KERNEL_MODULES=true sh "$T/step.sh" ) >"$T/out" 2>&1 || CASE_RC=$?
check "missing-kmod guard rc" 1 "$CASE_RC"

echo "test_build_deps: $pass passed, $fail failed"
[ "$fail" = 0 ]