#!/bin/sh
# Install-and-verify one batch of zte-mesh packages on a lab machine (PLAN C10). Runs on the router under busybox
# ash; drive it from the workspace with tools/lab/rssh.py. Output is a list of `KEY=VALUE` evidence lines and a final
# PASS/FAIL, so one run can be pasted into docs/journal as proof and diffed against the next batch.
#
#   sh verify-install.sh                 # read the machine back and run the three smokes
#   sh verify-install.sh /tmp/zte-apk    # install the apks found there (upgrade in place), restart, then the same
#
# Passing a directory means "activate this batch", so the script restarts rpcd and the service after installing:
# `apk add` only replaces files. Measured on owrt -- after replacing luci-app-zte-mesh the status call still had no
# `local` key (rpcd kept the pre-split plugin) and the attach instance still reported itself running with zmesh0 gone.
# Verifying without the restart would have graded the previous build, not this one.
#
# The three smokes are the three things that can be wrong after an install and that the page does not tell you:
#   1 service     -- the daemon is up, and the role it reports is the role the configuration asks for
#   2 instances   -- exactly the procd instances that role implies are running (no missing applier, no stray one)
#   3 profile     -- what the Wi-Fi applier did last time it ran, or (on a controller that touches no radio) that it
#                    correctly did nothing at all
set -u

APK_DIR=${1:-}
UBUS=${UBUS:-ubus}
APK=${APK:-apk}
UCI=${UCI:-uci}
LOGREAD=${LOGREAD:-logread}
INIT_D=${INIT_D:-/etc/init.d}
RESTART_WAIT=${RESTART_WAIT:-12}
RUNDIR=${ZTE_MESH_RUNDIR:-/run/zte-mesh}
PLAN=$RUNDIR/wlan-plan
SYNC_STATUS=$RUNDIR/wlan-sync.status
PKG_STEMS='zte-mesh zte-mesh-launcher luci-app-zte-mesh luci-i18n-zte-mesh-zh-cn'

pass=0
fail=0
installed_any=
S_ROLE=''
S_LOCAL=''
S_RUNNING=''
S_INSTALLED=''
S_ATTACHED=''

say() { printf '%s=%s\n' "$1" "$2"; }
check() {
	# check LABEL EXPECTED ACTUAL -- a counted assertion whose line is also the evidence
	if [ "$2" = "$3" ]; then
		pass=$((pass + 1))
		say "$1" "$3"
	else
		fail=$((fail + 1))
		printf 'FAIL %s expected=%s got=%s\n' "$1" "$2" "$3"
	fi
}

# --- install -------------------------------------------------------------------------------------------------------
# One `apk add` naming a package this machine cannot take does not half-succeed, it refuses the whole transaction
# (measured on the x86_64 VM, which never accepts the aarch64 launcher). So each file is its own call and a file that
# is simply absent is a skip, not a failure.
install_apks() {
	[ -n "$APK_DIR" ] || { say install skipped; return 0; }
	[ -d "$APK_DIR" ] || { fail=$((fail + 1)); printf 'FAIL apk_dir dir=%s\n' "$APK_DIR"; return 1; }
	for stem in $PKG_STEMS; do
		# [0-9] after the stem: `zte-mesh-*.apk` would otherwise also match zte-mesh-launcher-*.apk
		# and the loop would install the launcher twice while zte-mesh itself never lands.
		pkg=$(ls "$APK_DIR/$stem"-[0-9]*.apk 2>/dev/null | sort | tail -n 1)
		if [ -z "$pkg" ]; then
			say "install_$stem" skip
			continue
		fi
		out=$($APK add --allow-untrusted "$pkg" 2>&1)
		rc=$?
		if [ "$rc" = 0 ]; then
			installed_any=yes
			say "install_$stem" "$(basename "$pkg")"
		elif echo "$out" | grep -qi 'installable'; then
			# architecture mismatch is the expected answer on a machine that cannot carry this package
			say "install_$stem" uninstallable
		else
			fail=$((fail + 1))
			say "install_$stem" "rc=$rc"
			echo "$out" | tail -3
		fi
	done
}

restart_services() {
	[ "$installed_any" = yes ] || { say restart not_needed; return 0; }
	$INIT_D/rpcd restart >/dev/null 2>&1
	$INIT_D/zte-mesh restart >/dev/null 2>&1
	# The init brings the instances up asynchronously and attach needs a second to create and enslave zmesh0;
	# reading the machine back before that settles reports the old instance set.
	sleep "$RESTART_WAIT"
	say restart rpcd+zte-mesh
}

versions() {
	for stem in $PKG_STEMS; do
		# [0-9] after the stem: without it `zte-mesh-` also matches zte-mesh-launcher-
		v=$($APK list --installed 2>/dev/null | sed -n "s|^$stem-\([0-9][^ ]*\) .*|\1|p" | head -1)
		say "version_$stem" "${v:-absent}"
	done
}

# --- reading the machine back --------------------------------------------------------------------------------------
json_get() {
	# json_get DOC KEY -- one sed over the lines, no jsonfilter: `ubus call` pretty-prints this document with one key
	# per line, the five keys below are flat, and the first line matching wins (the status call prints its own keys
	# before the nested `state`, so the outer `running` is always the one that matches).
	printf '%s' "$1" | sed -n "s/.*\"$2\": *\"\{0,1\}\([a-z0-9_-]*\)\"\{0,1\}.*/\1/p" | head -1
}

kv_get() {
	# kv_get FILE KEY -- /run/zte-mesh/wlan-sync.status is plain key=value lines on purpose (see wlan-sync's
	# write_status), so parsing it must not pretend it is JSON.
	[ -s "$1" ] && sed -n "s/^$2=//p" "$1" | head -1
}

instances_running() {
	# `ubus call service list` pretty-prints one document; the instance names sit at three tabs and their fields at
	# four, so depth is what separates an instance's own "running" from the nested blocks around it.
	$UBUS call service list '{"name":"zte-mesh"}' 2>/dev/null | awk '
		/^[\t]+"[a-z]/ {
			depth = 0
			while (substr($0, depth + 1, 1) == "\t") depth++
			key = $1
			gsub(/["{:]/, "", key)
			if (depth == 3) { inst = key; seen[inst] = "stopped" }
			else if (depth == 4 && key == "running" && $0 ~ /true/) seen[inst] = "running"
		}
		END { for (i in seen) printf "%s=%s ", i, seen[i] }
	'
}

smoke_service() {
	status=$($UBUS call luci.zte-mesh status 2>/dev/null)
	if [ -z "$status" ]; then
		fail=$((fail + 1))
		say smoke_service no-status
		return 1
	fi
	S_INSTALLED=$(json_get "$status" installed)
	S_RUNNING=$(json_get "$status" running)
	S_ATTACHED=$(json_get "$status" attached)
	S_ROLE=$(json_get "$status" role)
	S_LOCAL=$(json_get "$status" local)
	check service_installed true "$S_INSTALLED"
	check service_running true "$S_RUNNING"
	say service_role "$S_ROLE"
	# The shipped status call always prints `local` (luci.zte-mesh's status branch). An empty answer therefore is not
	# a configuration state: the running rpcd still has the plugin it loaded before this install, so the page reads the
	# old document even though the file on disk is new. `apk add` replaces the file, it does not reload rpcd.
	[ -n "$S_LOCAL" ] || S_LOCAL=absent
	if [ "$S_LOCAL" = absent ]; then
		fail=$((fail + 1))
		printf 'FAIL service_local reported=absent hint=%s\n' "rpcd still has the pre-install plugin: /etc/init.d/rpcd restart"
	else
		say service_local "$S_LOCAL"
	fi
	say service_attached "$S_ATTACHED"
	say uci_mesh_role "$($UCI -q get zte_mesh.main.mesh_role 2>/dev/null || echo unset)"
	[ -n "$S_ROLE" ] || { fail=$((fail + 1)); say service_role empty; return 1; }
	return 0
}

smoke_instances() {
	all=$(instances_running)
	say instances "${all% }"
	# controller and attach belong to every role; the applier and the search loop do not, and which ones do is the
	# same judgement the init used when it opened the instances.
	for want in controller attach; do
		got=absent
		for pair in $all; do
			[ "${pair%%=*}" = "$want" ] && got=${pair#*=}
		done
		check "instance_$want" running "$got"
	done
	if [ "$S_ROLE" = master ] && [ "$S_LOCAL" != true ]; then
		# a controller that leaves its own radios alone must have no applier; one that shows up means the restart
		# signature now claims an instance the role does not run
		check instance_wlan-sync absent "$(pair_value "$all" wlan-sync)"
	else
		check instance_wlan-sync running "$(pair_value "$all" wlan-sync)"
	fi
	# The search loop is the controller half of the enrolment handshake; a child has nothing to search for.
	if [ "$S_ROLE" = master ]; then
		check instance_search running "$(pair_value "$all" search)"
	else
		check instance_search absent "$(pair_value "$all" search)"
	fi
}

pair_value() {
	# pair_value LIST KEY
	for pair in $1; do
		[ "${pair%%=*}" = "$2" ] && { printf '%s' "${pair#*=}"; return 0; }
	done
	printf absent
}

# The main BSS of a band is applied onto the router's own section (wlan-sync's ap_iface_of picks default_* and
# writes ssid/key there), so on a slave the visible result is not a `zte_*` section -- it is the SSID of the
# interface that was already there. Reading only zte_* names would report `none` on a machine where the applier
# did exactly the right thing.
smoke_ssid() {
	[ -s "$PLAN" ] || { say profile_ssid no-plan; return 0; }
	rows=$(sed -n 's/^rows=\([0-9]*\)$/\1/p' "$PLAN" | head -1)
	[ -n "$rows" ] || { say profile_ssid no-rows; return 0; }
	i=0
	while [ "$i" -lt "$rows" ]; do
		enabled=$(sed -n "s/^$i\.enabled=//p" "$PLAN" | head -1)
		ssid=$(sed -n "s/^$i\.ssid=//p" "$PLAN" | head -1)
		if [ "$enabled" = 1 ] && [ -n "$ssid" ]; then
			# the same ssid may legitimately exist on more than one section; the evidence is the section that has it
			# `uci show` prints values in single quotes (wireless.default_radio2.ssid='FRESHR10'), so the quotes have
			# to be optional -- matching only the bare form calls a healthy applier a missing SSID. Measured on ax3600,
			# where FRESHR10 and FRESHR10-5G were both live and the first version of this smoke reported absent.
			# `|` as the sed delimiter and every sed-special character escaped: an SSID is user data and can contain
			# `/`, `.` or `+`, which would otherwise break the expression or match something else.
			where=$($UCI show wireless 2>/dev/null |
				sed -n "s|^wireless\.\([^.=]*\)\.ssid='\{0,1\}$(printf '%s' "$ssid" | sed 's/[][\\.*^$/|]/\\&/g')'\{0,1\}$|\1|p" |
				tr '\n' ' ')
			where=${where% }
			if [ -n "$where" ]; then
				pass=$((pass + 1))
				say "plan_ssid_$i" "$ssid@$where"
			else
				fail=$((fail + 1))
				printf 'FAIL plan_ssid_%s expected=%s got=absent\n' "$i" "$ssid"
			fi
		fi
		i=$((i + 1))
	done
}

smoke_profile() {
	say plan_exists "$([ -s "$PLAN" ] && echo yes || echo no)"
	ours=$($UCI show wireless 2>/dev/null | sed -n 's/^wireless\.\(zte_[^.]*\)=wifi-iface$/\1/p' | tr '\n' ' ')
	ours=${ours% }
	if [ "$S_ROLE" = master ] && [ "$S_LOCAL" != true ]; then
		# nothing may reach this router's radios: an applier result or one of our sections is the failure
		check profile_no_status_file absent "$([ -e "$SYNC_STATUS" ] && echo present || echo absent)"
		check profile_sections unset "${ours:-unset}"
		return 0
	fi
	if [ ! -s "$SYNC_STATUS" ]; then
		fail=$((fail + 1))
		say smoke_profile no-status-file
		return 1
	fi
	say sync_result "$(kv_get "$SYNC_STATUS" result)"
	say sync_changed "$(kv_get "$SYNC_STATUS" changed)"
	say sync_bands "$(kv_get "$SYNC_STATUS" bands)"
	say sync_message "$(kv_get "$SYNC_STATUS" message)"
	check sync_result ok "$(kv_get "$SYNC_STATUS" result)"
	say profile_sections "${ours:-none}"
	return 0
}

smoke_log() {
	# Not a smoke of its own, one number: it catches the shape where everything above passes because the service
	# restarted out from under the check. The trace socket retry pair is excluded on purpose -- ns-run binds and
	# rebinds /var/tmp/zte-meshctl0._snd and logs the EADDRINUSE/ECONNREFUSED on the way (dozens of lines per hour on
	# the lab controller), so counting it would make the number meaningless. What is left is what nobody has explained.
	err=$($LOGREAD 2>/dev/null | grep -i zte-mesh | grep -viE 'ZTE_CONTROLLER_TRACE unix_bind' |
		grep -ciE 'error|failed|errno=[1-9]|crash loop')
	say log_error_lines "${err:-0}"
}

install_apks
restart_services
versions
if smoke_service && smoke_instances && smoke_profile; then :; fi
# only where an applier is supposed to run: a controller that leaves its own radios alone has no plan to honour
if [ "$S_ROLE" = master ] && [ "$S_LOCAL" != true ]; then :; else smoke_ssid; fi
smoke_log

say pass "$pass"
say fail "$fail"
[ "$fail" = 0 ] && say RESULT PASS || say RESULT FAIL
[ "$fail" = 0 ]
