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
LIBEXEC=$T/libexec
ZTE_MESH_LIBEXEC=$LIBEXEC
mkdir -p "$ROOTFS/root/lib" "$ROOTFS/root/bin" "$LIBEXEC"
export UBUS APK UCI LOGREAD INIT_D RESTART_WAIT SSID_WAIT SSID_POLL ZTE_MESH_RUNDIR ZTE_MESH_ROOTFS ZTE_MESH_LIBEXEC

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
# the read-back greps this file for the batch's own marker: a child that borrows its seed from the air defines
# live_ap_iface, and the r17 script on the lab machines has zero hits for it -- so the string here is the thing
# a packaged shell change leaves behind when nothing in the rootfs tarball moves.
if [ "\$1" = restart ]; then
	echo '$name' >> '$FIX/restarted'
	# the applier's write, once per line this fixture says the restart made. Only zte-mesh: rpcd does not touch
	# /etc/config/wireless, and the log is the only witness of a restart that renamed the network.
	if [ '$name' = zte-mesh ] && [ -f '$FIX/writes' ]; then
		n=\$(cat '$FIX/writes'); i=0
		while [ "\$i" -lt "\$n" ]; do
			echo 'Mon Nov  3 09:00:00 UTC 2026 zte-mesh: applied; wireless committed and reloaded' >> '$FIX/log'
			i=\$((i + 1))
		done
	fi
	# the ring can roll over inside the window: the log left after the restart then holds fewer of those lines than
	# the log before it. That is not a quiet restart -- it is a truncated window, and the delta is not a number.
	if [ '$name' = zte-mesh ] && [ -f '$FIX/wrapped' ]; then cat '$FIX/wrapped' > '$FIX/log'; fi
fi
exit 0
EOF
	chmod +x "$INIT_D/$name"
done

# the applier the package installs next to the init script; the read-back wants the flag comparison this batch added
cat > "$LIBEXEC/wlan-sync" <<'EOF'
#!/bin/sh
set_flag() { :; }
EOF
chmod +x "$LIBEXEC/wlan-sync"

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
printf 'Mon Nov  2 10:00:00 UTC 2026 ns-run[411]: ZTE_MESH_SERVICE cfg mode=1 set_role_enable=0 en_master_only=0 cur_hop=2 max_hop=2 epoch=1793000000\n' >> "$FIX/log"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line RESULT PASS
check_line restart not_needed
check_line applier_writes no-restart
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
# the child's own hop count: the startup fixture writes -2 and nothing else in our code overwrites it on this role,
# so a 2 here can only be the vendor daemon's own update (B16).
check_line cur_hop 2

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
check_line init_seed_from_air yes
check_line sync_flag_by_meaning yes
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

# --- the batch that only moves shell files: the package itself is the witness ------------------------------------------
# r18 changes `zte-mesh.init` and `wlan-sync` and nothing in the rootfs, so `packed_copy` stays `yes` on a machine
# still running the old scripts. A batch whose fix is invisible to the read-back is a batch that can be reported as
# installed while it is not installed.
CASE=scripts-stale
packed_libs
sed -i 's/live_ap_iface/borrowed_seed/' "$INIT_D/zte-mesh"
printf '#!/bin/sh\n' > "$LIBEXEC/wlan-sync"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 1
check_line RESULT FAIL
for label in init_seed_from_air sync_flag_by_meaning; do
	if grep -q "^FAIL $label expected=yes got=no" "$out"; then pass=$((pass + 1)); else
		fail=$((fail + 1)); echo "FAIL case $CASE: an $label that is not in the package must fail the run"; fi
done
# the other two markers answer yes: the old machine is not stale in that direction, and the failure must be
# attributable to the one thing that is missing
check_line trace_per_bss_name yes
check_line probe_bss_detail yes
sed -i 's/borrowed_seed/live_ap_iface/' "$INIT_D/zte-mesh"

# and a machine where the applier file simply is not there is not a stale one either
CASE=scripts-absent
packed_libs
printf '#!/bin/sh\nset_flag() { :; }\n' > "$LIBEXEC/wlan-sync"
rm -f "$LIBEXEC/wlan-sync"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line init_seed_from_air yes
check_line sync_flag_by_meaning unreadable
if grep -q '^FAIL ' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: an absent applier file is an environment fact"; else pass=$((pass + 1)); fi
printf '#!/bin/sh\nset_flag() { :; }\n' > "$LIBEXEC/wlan-sync"

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

# --- case 13: a restart of a converged child writes nothing (B8b, PLAN H-14) ------------------------------------------
# The child's seed used to be the profile declared on this router, so every restart renamed the air and the
# controller's M2 renamed it back. `/etc/config/wireless` ends up identical either way -- the number of applier
# writes inside the restart window is the only thing that tells the two apart.
CASE=child-restart-quiet
status_json true true true slave false > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance wlan-sync true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'slave\n' > "$FIX/role"
printf "wireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid='FRESHR10'\nwireless.default_radio1=wifi-iface\nwireless.default_radio1.ssid='FRESHR10-5G'\n" > "$FIX/wireless"
plan_rows FRESHR10 FRESHR10-5G
printf 'epoch=1\nresult=ok\nchanged=0\nbands=\nmessage=\n' > "$RUN/wlan-sync.status"
printf 'Mon Nov  3 08:00:00 UTC 2026 zte-mesh: plan applied nothing new\n' > "$FIX/log"
rm -f "$FIX/writes"
RESTART_NOW=1 sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line RESULT PASS
check_line restart now
check_line air_matched_plan yes
check_line applier_writes 0

# --- case 14: the same machine with the old seed writes twice ---------------------------------------------------------
# This is r17's shape: the air already carried the controller's pair, the restart still renamed it and renamed it
# back. It must not pass on the strength of the file looking fine afterwards.
CASE=child-restart-renamed
printf '2\n' > "$FIX/writes"
RESTART_NOW=1 sh "$SCRIPT" > "$out" 2>&1
check_rc $? 1
check_line RESULT FAIL
if grep -q 'FAIL applier_writes expected=0 got=2' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: a restart that renamed the network must fail on the write count"; fi
rm -f "$FIX/writes"

# --- case 15: a child that still had to converge is evidence, not a fault ---------------------------------------------
# Fresh install, first run: the air carries the router's own network, so the applier has work to do and one write is
# correct. Judging that would call a healthy install broken -- the same class of mistake as grading the SSID too early.
CASE=child-restart-converging
printf "wireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid='OldLocalName'\n" > "$FIX/wireless"
printf "wireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid='FRESHR10'\n" > "$FIX/wireless-landed"
plan_rows FRESHR10
printf '2\n' > "$FIX/writes"
rm -f "$FIX/count"
cat > "$BIN/uci" <<EOF
#!/bin/sh
case "\$*" in
*-q\ get*)   sed -n '1p' '$FIX/role' ;;
*show\ wireless*)
	n=0
	[ -f '$FIX/count' ] && n=\$(cat '$FIX/count')
	n=\$((n + 1)); printf '%s\n' "\$n" > '$FIX/count'
	# the first read is the one taken before the restart; from the settle pass on, the plan has landed
	if [ "\$n" -ge 2 ]; then cat '$FIX/wireless-landed'; else cat '$FIX/wireless'; fi ;;
*) exit 1 ;;
esac
EOF
chmod +x "$BIN/uci"
RESTART_NOW=1 sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line RESULT PASS
check_line air_matched_plan rows-unset:0
check_line applier_writes 2
if grep -q 'FAIL applier_writes' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: nothing to judge while the profile is still landing"; else pass=$((pass + 1)); fi
rm -f "$FIX/writes" "$FIX/wireless-landed"

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

# a controller that touches no radio has no applier to count -- and asking for the restart must not invent one
CASE=controller-restart-no-applier
status_json true true true master false > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance search true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'master\n' > "$FIX/role"
rm -f "$RUN/wlan-sync.status"
printf 'Mon Nov  3 08:00:00 UTC 2026 zte-mesh: plan applied nothing new\n' > "$FIX/log"
RESTART_NOW=1 sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line applier_writes no-applier
if grep -q '^air_matched_plan' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: the write window is not this role's business"; else pass=$((pass + 1)); fi

# --- case 16: no log to count -- zero lines is not a quiet restart ----------------------------------------------------
# `grep -c` on an unreadable log answers 0, and 0 - 0 is exactly the shape of the verdict we want. The only honest
# answer on a machine that exposes no log is "I did not look".
CASE=child-restart-no-logread
status_json true true true slave false > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance wlan-sync true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'slave\n' > "$FIX/role"
printf "wireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid='FRESHR10'\nwireless.default_radio1=wifi-iface\nwireless.default_radio1.ssid='FRESHR10-5G'\n" > "$FIX/wireless"
plan_rows FRESHR10 FRESHR10-5G
printf 'epoch=1\nresult=ok\nchanged=0\nbands=\nmessage=\n' > "$RUN/wlan-sync.status"
rm -f "$FIX/log" "$FIX/writes" "$FIX/wrapped"
RESTART_NOW=1 sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line applier_writes no-logread
if grep -q '^FAIL applier_writes' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: a log that cannot be read is not a fault"; else pass=$((pass + 1)); fi

# --- case 17: the log ring rolled over inside the window --------------------------------------------------------------
# Three committed lines before the restart, none after: the window was truncated, so the delta would be -3.
CASE=child-restart-wrapped
printf 'Mon Nov  3 08:00:00 UTC 2026 zte-mesh: applied; wireless committed and reloaded\n' > "$FIX/log"
printf 'Mon Nov  3 08:00:00 UTC 2026 zte-mesh: applied; wireless committed and reloaded\n' >> "$FIX/log"
printf 'Mon Nov  3 08:00:00 UTC 2026 zte-mesh: applied; wireless committed and reloaded\n' >> "$FIX/log"
printf 'Mon Nov  3 08:30:00 UTC 2026 zte-mesh: plan applied nothing new\n' > "$FIX/wrapped"
RESTART_NOW=1 sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line applier_writes wrapped
if grep -q '^FAIL applier_writes' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: a truncated window is not a count"; else pass=$((pass + 1)); fi
rm -f "$FIX/wrapped"

# --- case 18: the child's hop count is still the `not attached` the fixture wrote (B16) --------------------------------
# -2 is what our own startup path writes for a child; the daemon is supposed to replace it. A machine that has been
# up and still reads -2 has an agent that never learned its hop count -- that is the fault this line exists to catch.
CASE=cur-hop-not-attached
printf 'Mon Nov  3 09:00:00 UTC 2026 ns-run[77]: ZTE_MESH_SERVICE cfg mode=1 set_role_enable=0 en_master_only=0 cur_hop=-2 max_hop=2 epoch=1793678000\n' > "$FIX/log"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 1
check_line RESULT FAIL
if grep -q 'FAIL cur_hop expected=updated got=-2' "$out"; then pass=$((pass + 1)); else
	fail=$((fail + 1)); echo "FAIL case $CASE: a child that never updated CurHop must be named"; fi

# --- case 19: no cfg line in the ring yet is not a verdict ------------------------------------------------------------
CASE=cur-hop-no-line
printf 'Mon Nov  3 09:00:00 UTC 2026 zte-mesh: plan applied nothing new\n' > "$FIX/log"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line cur_hop no-cfg-line
if grep -q '^FAIL cur_hop' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: nothing observed is not a failure"; else pass=$((pass + 1)); fi

# -98 is the supervisor's own `could not read the column`, which is not a hop count either
CASE=cur-hop-unreadable
printf 'Mon Nov  3 09:00:00 UTC 2026 ns-run[77]: ZTE_MESH_SERVICE cfg mode=1 set_role_enable=0 en_master_only=0 cur_hop=-98 max_hop=-98 epoch=1793678000\n' > "$FIX/log"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line cur_hop -98
if grep -q '^FAIL cur_hop' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: an unreadable column is the service's problem, not this batch's"; else pass=$((pass + 1)); fi

# a controller writes this column itself (multiap-cfg.c), so its 2 says nothing about the agent: evidence only
CASE=cur-hop-controller-evidence
status_json true true true master false > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance search true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'master\n' > "$FIX/role"
rm -f "$RUN/wlan-sync.status"
plan_rows SHOULD-NOT-BE-APPLIED
printf 'Mon Nov  3 09:00:00 UTC 2026 ns-run[88]: ZTE_MESH_SERVICE cfg mode=2 set_role_enable=0 en_master_only=1 cur_hop=-2 max_hop=2 epoch=1793678000\n' > "$FIX/log"
sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line cur_hop -2
if grep -q '^FAIL cur_hop' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: this role is not the agent path"; else pass=$((pass + 1)); fi

# and a restart is not the window for this number: the daemon writes the column seconds to minutes after a fresh
# start, and the new supervisor's own baseline snapshot can land after we look -- the C10 lesson, one column over.
CASE=cur-hop-after-restart
status_json true true true slave false > "$FIX/status"
{
	printf '{\n\t"zte-mesh": {\n\t\t"instances": {\n'
	instance controller true
	instance attach true
	instance wlan-sync true
	printf '\t\t}\n\t}\n}\n'
} > "$FIX/svc"
printf 'slave\n' > "$FIX/role"
printf "wireless.default_radio0=wifi-iface\nwireless.default_radio0.ssid='FRESHR10'\nwireless.default_radio1=wifi-iface\nwireless.default_radio1.ssid='FRESHR10-5G'\n" > "$FIX/wireless"
plan_rows FRESHR10 FRESHR10-5G
printf 'epoch=1\nresult=ok\nchanged=0\nbands=\nmessage=\n' > "$RUN/wlan-sync.status"
printf 'Mon Nov  3 09:00:00 UTC 2026 ns-run[77]: ZTE_MESH_SERVICE cfg mode=1 set_role_enable=0 en_master_only=0 cur_hop=-2 max_hop=2 epoch=1793678000\n' > "$FIX/log"
RESTART_NOW=1 sh "$SCRIPT" > "$out" 2>&1
check_rc $? 0
check_line cur_hop -2
if grep -q '^FAIL cur_hop' "$out"; then fail=$((fail + 1)); echo "FAIL case $CASE: the run that just restarted the supervisor cannot grade its hop count"; else pass=$((pass + 1)); fi

printf 'verify-install test: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ]
