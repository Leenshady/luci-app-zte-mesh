#!/bin/sh
# Host test for tools/lab/verify-install.sh without a router: the four commands it reads through (ubus / apk / uci /
# logread) are the same names the script takes from its own environment, so the stubs answer from fixtures and the
# assertions are about what the script decided. Run with the shell OpenWrt uses: busybox ash tests/test_verify_install.sh
#
# What this test guards is the judgement, not the router: which instance set a role implies, which keys of the status
# document are read, what "nothing should have been written here" means on a controller that touches no radio. Those
# are the parts that silently rot when the init or the status call changes.
SCRIPT=${1:-$(dirname "$0")/../tools/lab/verify-install.sh}
[ -f "$SCRIPT" ] || { echo "verify-install.sh not found: $SCRIPT" >&2; exit 1; }
T=$(mktemp -d) || exit 1
T=$(cd "$T" && pwd)
trap 'rm -rf "$T"' EXIT
BIN=$T/bin
FIX=$T/fix
RUN=$T/run
mkdir -p "$BIN" "$FIX" "$RUN"

# The script takes its four commands and its run directory from the environment precisely so this test can point them
# at the fixtures. Forgetting one of these would make the test read the real machine (or fail on a missing ubus).
UBUS=$BIN/ubus
APK=$BIN/apk
UCI=$BIN/uci
LOGREAD=$BIN/logread
INIT_D=$T/init.d
ROOTFS=$T/rootfs
RESTART_WAIT=0
# the profile smoke is the one that waits; the fixtures answer immediately, so no case pays for a settle
SSID_WAIT=0
SSID_POLL=1
ZTE_MESH_RUNDIR=$RUN
ZTE_MESH_ROOTFS=$ROOTFS
mkdir -p "$ROOTFS/root/lib" "$ROOTFS/root/bin"
export UBUS APK UCI LOGREAD INIT_D RESTART_WAIT SSID_WAIT SSID_POLL ZTE_MESH_RUNDIR ZTE_MESH_ROOTFS

pass=0
fail=0
out=$T/out

check_line() {
	# check_line LABEL EXPECTED -- the evidence line `LABEL=EXPECTED` must be there
	if grep -qx "$1=$2" "$out"; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
		printf 'FAIL case %s: expected %s=%s, got:\n' "$CASE" "$1" "$2"
		sed -e 's/^/    /' "$out"
	fi
}
check_rc() {
	if [ "$1" = "$2" ]; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
		printf 'FAIL case %s: exit expected=%s got=%s\n' "$CASE" "$1" "$2"
	fi
}

# --- stubs ----------------------------------------------------------------------------------------------------------
cat > "$BIN/ubus" <<EOF
#!/bin/sh
case "\$*" in
*luci.zte-mesh\ status*) cat '$FIX/status' ;;
*"service list"*)         cat '$FIX/svc' ;;
*) exit 1 ;;
esac
EOF
cat > "$BIN/uci" <<EOF
#!/bin/sh
case "\$*" in
*-q\ get*)   sed -n '1p' '$FIX/role' ;;
*show\ wireless*) cat '$FIX/wireless' ;;
*) exit 1 ;;
esac
EOF
cat > "$BIN/apk" <<EOF
#!/bin/sh
case "\$1" in
add)
	if [ -f '$FIX/add-fails' ]; then echo 'ERROR: unable to select packages:'; exit 1; fi
	if [ -f '$FIX/uninstallable' ]; then echo 'ERROR: zte-mesh-launcher-0.1.0-r2: not installable'; exit 1; fi
	# naming the file it took is how the test catches the stem-glob confusion (zte-mesh swallowing the launcher apk)
	echo "ADDED $(basename "${2##*/}")"; exit 0 ;;
list) cat '$FIX/installed' ;;
*) exit 1 ;;
esac
EOF
cat > "$BIN/logread" <<EOF
#!/bin/sh
[ -f '$FIX/log' ] && cat '$FIX/log'
EOF
chmod +x "$BIN/ubus" "$BIN/uci" "$BIN/apk" "$BIN/logread"
# The two service scripts an install has to activate; each records that it was asked to restart.
mkdir -p "$INIT_D"
for name in rpcd zte-mesh; do
	cat > "$INIT_D/$name" <<EOF
#!/bin/sh
[ "\$1" = restart ] && echo '$name' >> '$FIX/restarted'
exit 0
EOF
	chmod +x "$INIT_D/$name"
done

# plan_rows SSID... -- /run/zte-mesh/wlan-plan is a key=value table, one row per BSS the applier was told to carry.
# The SSID smoke compares those rows against the wireless config, so the fixtures have to look like the real file.
plan_rows() {
	: > "$RUN/wlan-plan"
	printf 'rows=%d\n' "$#" >> "$RUN/wlan-plan"
	i=0
	for ssid in "$@"; do
		{
			echo "$i.radio=$i"
			echo "$i.slot=0"
			echo "$i.instance=DEV.WIFI.AP$i"
			echo "$i.ssid=$ssid"
			echo "$i.key=Ztemesh2631!"
			echo "$i.enabled=1"
			echo "$i.hidden=0"
			echo "$i.beacon_type=12"
		} >> "$RUN/wlan-plan"
		i=$((i + 1))
	done
}

# An instance's own `running` sits at four tabs and its name at three, exactly like `ubus call service list`.
instance() {
	printf '\t\t\t"%s": {\n' "$1"
	printf '\t\t\t\t"running": %s,\n' "$2"
	printf '\t\t\t\t"command": [\n\t\t\t\t\t"/usr/libexec/zte-mesh/%s"\n\t\t\t\t]\n' "$1"
	printf '\t\t\t}\n'
}
status_json() {
	# the same key order and the same nesting as openwrt/zte-mesh/files/luci.zte-mesh's status branch
	printf '{\n\t"installed": %s,\n\t"running": %s,\n\t"attached": %s,\n' "$1" "$2" "$3"
	printf '\t"role": "%s",\n\t"local": %s,\n\t"sync": null,\n\t"age": 1,\n' "$4" "$5"
	printf '\t"state": { "running": true, "schema": 1 }\n}\n'
}

# --- case 1: a child that is fine -----------------------------------------------------------------------------------
CASE=child-healthy
status_json true true true slave false > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance wlan-sync true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'slave\n' > "$FIX/role"
printf 'wireless.zte_backhaul_sta_5g=wifi-iface\nwireless.zte_backhaul_sta_5g.ssid=%sFRESHR10-5G%s\nwireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid=%sFRESHR10%s\n' "'" "'" "'" "'" > "$FIX/wireless"
printf 'zte-mesh-0.1.0-r15 noarch {/x} (GPL-2.0-only) [installed]\nzte-mesh-launcher-0.1.0-r2 aarch64_cortex-a53 {/y} (GPL-2.0-only) [installed]\nluci-app-zte-mesh-0.1.0-r8 noarch {/z} (GPL-2.0-only) [installed]\n' > "$FIX/installed"
printf 'epoch=1793000000\nresult=ok\nchanged=1\nbands=2g 5g\nmessage=\n' > "$RUN/wlan-sync.status"
plan_rows FRESHR10 FRESHR10-5G
printf 'Mon Nov  2 10:00:00 UTC 2026 zte-mesh: plan applied\n' > "$FIX/log"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line RESULT PASS
check_line restart not_needed
check_line service_role slave
check_line instance_wlan-sync running
check_line instance_search absent
check_line sync_result ok
check_line sync_bands '2g 5g'
check_line profile_sections zte_backhaul_sta_5g
check_line plan_ssid_0 FRESHR10@default_radio0
check_line plan_ssid_1 'FRESHR10-5G@zte_backhaul_sta_5g'
check_line version_zte-mesh 0.1.0-r15
check_line version_luci-i18n-zte-mesh-zh-cn absent
check_line log_error_lines 0

# profile_sections is a whole line, so compare it as text rather than through the value-splitting helper
if grep -q '^profile_sections=zte_backhaul_sta_5g$' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: profile_sections should name only our own section"; fi

# --- case 2: a controller that leaves its own radios alone -----------------------------------------------------------
CASE=controller-only
status_json true true true master false > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance search true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'master\n' > "$FIX/role"
printf 'wireless.default_2g=wifi-iface\nwireless.default_5g=wifi-iface\n' > "$FIX/wireless"
rm -f "$RUN/wlan-sync.status"
plan_rows SHOULD-NOT-BE-APPLIED
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line RESULT PASS
check_line service_local false
check_line instance_wlan-sync absent
check_line instance_search running
check_line profile_no_status_file absent
check_line profile_sections unset
if grep -q '^plan_ssid_' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: a controller that applies nothing has no SSID to answer for"; else pass=$((pass + 1)); fi

# the same machine with an applier instance present is the signature bug this catches: the init stopped opening the
# instance but something still thinks it runs, or restart.sig now claims one the role never had
CASE=controller-only-stray-applier
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance search true
	instance wlan-sync true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 1
check_line RESULT FAIL
if grep -q 'FAIL instance_wlan-sync expected=absent got=running' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: a stray applier must be the thing that fails"; fi

# --- case 3: a broadcasting controller (apply_local=1) ----------------------------------------------------------------
CASE=broadcasting-controller
status_json true true true master true > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance search true
	instance wlan-sync true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'master\n' > "$FIX/role"
printf "wireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid='FRESHR10'\nwireless.zte_extra_2g_1=wifi-iface\nwireless.zte_extra_2g_1.ssid='FRESHR10+5G.bak'\nwireless.zte_backhaul_5g=wifi-iface\n" > "$FIX/wireless"
plan_rows FRESHR10 'FRESHR10+5G.bak'
printf 'epoch=1\nresult=ok\nchanged=0\nbands=2g\nmessage=wpad-basic has no Multi-AP support\n' > "$RUN/wlan-sync.status"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line RESULT PASS
check_line service_local true
check_line instance_wlan-sync running
check_line sync_result ok
if grep -q '^profile_sections=zte_extra_2g_1 zte_backhaul_5g$' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: both of our sections must be named, in the order uci lists them"; fi
check_line plan_ssid_0 FRESHR10@default_radio0
check_line plan_ssid_1 'FRESHR10+5G.bak@zte_extra_2g_1'
if grep -q '^sync_message=wpad-basic has no Multi-AP support$' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: the applier's own message is evidence and must be printed"; fi

# --- case 4: the service is not running at all ------------------------------------------------------------------------
CASE=stalled
status_json true false false master false > "$FIX/status"
printf 'FAIL smoke_service expected=got\n' >/dev/null
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 1
check_line RESULT FAIL
if grep -q 'FAIL service_running expected=true got=false' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: a stalled service must fail the run"; fi

# --- case 5: applier said error ---------------------------------------------------------------------------------------
CASE=applier-error
status_json true true true slave false > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance wlan-sync true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'epoch=1\nresult=error\nchanged=0\nbands=\nmessage=the plan could not be applied\n' > "$RUN/wlan-sync.status"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 1
check_line RESULT FAIL
check_line sync_result error

# --- case 6: install reporting -----------------------------------------------------------------------------------------
CASE=install-mixed
printf 'not installable here\n' > "$FIX/uninstallable"
APK_DIR=$T/apks
mkdir -p "$APK_DIR"
: > "$APK_DIR/zte-mesh-0.1.0-r15.apk"
: > "$APK_DIR/zte-mesh-launcher-0.1.0-r2.apk"
: > "$APK_DIR/luci-app-zte-mesh-0.1.0-r8.apk"
sh "$SCRIPT" "$APK_DIR" > "$out" 2>&1
check_line install_zte-mesh uninstallable
check_line install_zte-mesh-launcher uninstallable
check_line install_luci-app-zte-mesh uninstallable
check_line install_luci-i18n-zte-mesh-zh-cn skip
if grep -qx 'RESULT=PASS' "$out" || grep -qx 'RESULT=FAIL' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: install must still end with a verdict"; fi

# the two verdicts must not be confused: a file that is absent is a skip, a file the machine refuses is a failure.
# And each stem must pick its own file -- zte-mesh-*.apk alone would match the launcher too.
CASE=install-clean
rm -f "$FIX/uninstallable" "$FIX/add-fails" "$FIX/restarted"
sh "$SCRIPT" "$APK_DIR" > "$out" 2>&1
check_line restart rpcd+zte-mesh
check_line install_zte-mesh zte-mesh-0.1.0-r15.apk
if [ "$(wc -l < "$FIX/restarted" 2>/dev/null | tr -d ' ')" = 2 ]; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: an install must restart both rpcd and zte-mesh"; fi
check_line install_zte-mesh-launcher zte-mesh-launcher-0.1.0-r2.apk
check_line install_luci-app-zte-mesh luci-app-zte-mesh-0.1.0-r8.apk
check_line install_luci-i18n-zte-mesh-zh-cn skip

# --- case 7: the running rpcd is older than the package on disk -------------------------------------------------------
# `apk add` replaces the file; it does not reload the plugin. The status document then has no `local` key at all
# (luci.zte-mesh's status branch only got it with the role split), and a script that read the empty string as
# "not local" would report a clean controller.
CASE=stale-rpcd
{
	printf '{\n\t"installed": true,\n\t"running": true,\n\t"attached": true,\n\t"role": "master",\n'
	printf '\t"sync": null,\n\t"age": 1,\n\t"state": { "running": true }\n}\n'
} > "$FIX/status"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 1
check_line RESULT FAIL
if grep -q 'FAIL service_local reported=absent hint=.*rpcd restart' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: a missing local key must name the reload, not look like apply_local=0"; fi

# --- case 8: the log count means "unexplained" --------------------------------------------------------------------------
# The trace socket retry pair is logged by the dozen per hour on a healthy controller; a crash loop is not normal.
CASE=log-noise
status_json true true true slave false > "$FIX/status"
printf 'zte-mesh: ns-run: ZTE_CONTROLLER_TRACE unix_bind path=/var/tmp/zte-meshctl0._snd status=-1 errno=98\nzte-mesh: ns-run: ZTE_CONTROLLER_TRACE unix_bind path=/var/tmp/zte-meshctl0._snd status=0 errno=111\n' > "$FIX/log"
printf 'epoch=1\nresult=ok\nchanged=1\nbands=2g\nmessage=\n' > "$RUN/wlan-sync.status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance wlan-sync true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
sh "$SCRIPT" > "$out" 2>&1
check_line log_error_lines 0
printf 'procd: Instance zte-mesh::search s in a crash loop 6 crashes, 300 seconds since last crash\n' >> "$FIX/log"
sh "$SCRIPT" > "$out" 2>&1
check_line log_error_lines 1

# --- case 9: the applier says ok but nothing carries the SSID ---------------------------------------------------------
# This is the shape the profile smoke exists for: wlan-sync reports result=ok after applying onto the radio's own
# section, so a check that only looks for `zte_*` sections sees `none` and calls the machine healthy. The plan names
# the SSIDs; the wireless config has to show at least one of them.
CASE=ssid-never-landed
status_json true true true slave false > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance wlan-sync true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'wireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid=OldLocalName\n' > "$FIX/wireless"
plan_rows FRESHR10
printf 'epoch=1\nresult=ok\nchanged=1\nbands=2g\nmessage=\n' > "$RUN/wlan-sync.status"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 1
check_line RESULT FAIL
if grep -q 'FAIL plan_ssid_0 expected=FRESHR10 got=absent' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: an SSID the applier claims but the config does not carry must fail"; fi
# the wait is what makes the FAIL mean "still wrong after the settle", so name the rows that never landed
check_line ssid_wait 0
check_line ssid_pending 0

# --- case 10: the installed copy is the one this batch wrote ---------------------------------------------------------
# The device copy cannot be hashed (apk strips the sections), so the script greps the two strings that only this
# batch's writer carries. A library without the per-BSS name still runs: the service sums an empty directory every
# second and falls back to the digest, so the page keeps the behaviour of the previous batch instead of breaking.
# That quiet fall-back is what these cases exist for.
packed_libs() {
	# packed_libs [stale] -- the two members the read-back greps; `stale` is the library from before write_bss()
	if [ "${1:-}" = stale ]; then
		printf 'light=%d metrics software ok\n' 1 > "$ROOTFS/root/lib/libzte_controller_trace.so"
	else
		printf 'light=%d metrics software .%%02x:%%02x:%%02x:%%02x:%%02x:%%02x.sta\n' 1 \
			> "$ROOTFS/root/lib/libzte_controller_trace.so"
	fi
	printf 'bss_detail\n' > "$ROOTFS/root/bin/db-engine-probe"
}
healthy_child() {
	status_json true true true slave false > "$FIX/status"
	{
		printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
		instance controller true
		instance attach true
		instance wlan-sync true
		printf '\t\t}\n\t}\n}\n'
	} > "$FIX/svc"
	printf 'slave\n' > "$FIX/role"
	printf 'wireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid=FRESHR10\n' > "$FIX/wireless"
	plan_rows FRESHR10
	printf 'epoch=1\nresult=ok\nchanged=1\nbands=2g\nmessage=\n' > "$RUN/wlan-sync.status"
}

CASE=packed-readback
healthy_child
packed_libs
mkdir -p "$RUN/peers"
printf 'light=0\n' > "$RUN/peers/02:00:00:00:19:e3.light"
printf 'hops=1\n' > "$RUN/peers/02:00:00:00:19:e3.metrics"
printf '{"nodes":[{"mac":"02:00:00:00:19:e3","stations":-1,"bss_detail":[]}]}\n' > "$RUN/state.json"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line RESULT PASS
check_line trace_per_bss_name yes
check_line probe_bss_detail yes
check_line peers_sta 0
check_line peers_software 0
# the lab link's only metrics digest is hops=1: no station count arrived at all, so an empty per-BSS count is not
# evidence against the decoder and must not be graded as one
check_line peer_reports_stations no
check_line state_bss_detail present

# the same machine with the files a real child produces: two BSS rows and a version string
CASE=packed-readback-full
printf '3\n' > "$RUN/peers/02:00:00:00:19:e3.02:00:00:00:19:e4.sta"
printf '1\n' > "$RUN/peers/02:00:00:00:19:e3.02:00:00:00:19:e5.sta"
printf 'V1.0.0.2B2.8000\n' > "$RUN/peers/02:00:00:00:19:e3.software"
printf 'stations=4\nhops=1\n' > "$RUN/peers/02:00:00:00:19:e3.metrics"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line peers_sta 2
check_line peers_software 1
check_line peer_reports_stations yes

CASE=packed-stale
packed_libs stale
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 1
check_line RESULT FAIL
if grep -q 'FAIL trace_per_bss_name expected=yes got=no' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: a library that never writes the per-BSS file must fail the run"; fi

# --- case 11: no rootfs on this machine is not a stale rootfs --------------------------------------------------------
# This harness has no private root at all; turning that environment fact into a batch verdict would fail every run.
CASE=packed-absent
rm -f "$ROOTFS/root/lib/libzte_controller_trace.so" "$ROOTFS/root/bin/db-engine-probe"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line trace_per_bss_name unreadable
check_line probe_bss_detail unreadable

# --- case 12: the profile lands a few seconds after the restart -------------------------------------------------------
# Measured on ax3600 (2026-11-03): after a restart the plan has no rows at t=0, its rows are there at t=2s while
# /etc/config/wireless still carries the previous profile, and the applier lands the new one at t=4s. Grading once
# right after the restart -- which is what INSTALL section 3 tells a reader to do after an install -- failed a
# healthy child. The stub answers with the old name for the first two `show wireless` calls (the profile smoke's
# own read, then the first settle pass) and with the landed name after that, so the wait is what turns it green.
CASE=ssid-late-landed
status_json true true true slave false > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance wlan-sync true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'slave\n' > "$FIX/role"
printf 'wireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid=OldLocalName\n' > "$FIX/wireless"
printf 'wireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid=FRESHR10\n' > "$FIX/wireless-landed"
plan_rows FRESHR10
printf 'epoch=1\nresult=ok\nchanged=1\nbands=2g\nmessage=\n' > "$RUN/wlan-sync.status"
rm -f "$FIX/count"
cat > "$BIN/uci" <<EOF
#!/bin/sh
case "\$*" in
*-q\ get*)   sed -n '1p' '$FIX/role' ;;
*show\ wireless*)
	n=0
	[ -f '$FIX/count' ] && n=\$(cat '$FIX/count')
	n=\$((n + 1)); printf '%s\n' "\$n" > '$FIX/count'
	if [ "\$n" -ge 3 ]; then cat '$FIX/wireless-landed'; else cat '$FIX/wireless'; fi ;;
*) exit 1 ;;
esac
EOF
chmod +x "$BIN/uci"
SSID_WAIT=6 SSID_POLL=2 sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line RESULT PASS
check_line ssid_wait 2
check_line plan_ssid_0 FRESHR10@default_radio0
if grep -q '^ssid_pending' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: a settled profile must not report pending rows"; else pass=$((pass + 1)); fi

# back to the stub every other case uses
cat > "$BIN/uci" <<EOF
#!/bin/sh
case "\$*" in
*-q\ get*)   sed -n '1p' '$FIX/role' ;;
*show\ wireless*) cat '$FIX/wireless' ;;
*) exit 1 ;;
esac
EOF
chmod +x "$BIN/uci"

printf 'verify-install test: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ]
