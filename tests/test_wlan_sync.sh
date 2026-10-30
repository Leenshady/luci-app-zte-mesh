#!/bin/sh
# Host test of openwrt/zte-mesh/files/wlan-sync: the plan the service publishes in the child (agent) role is
# mapped onto /etc/config/wireless. The UCI command line is replaced by a tiny key/value store with the same
# semantics for show/get/set/delete/commit, so the test runs anywhere.
#
#   sh tests/test_wlan_sync.sh [WLAN_SYNC]
WLAN_SYNC=${1:-$(dirname "$0")/../openwrt/zte-mesh/files/wlan-sync}
T=$(mktemp -d) || exit 1
# Git Bash's mktemp hands back a Windows drive prefix (C:\...), which the delete helpers on that host
# refuse; going through pwd gives the POSIX form there and changes nothing on Linux.
T=$(cd "$T" && pwd)
trap 'rm -rf "$T"' EXIT
DB=$T/db
mkdir -p "$DB" "$T/bin"
LOG=$DB.log

cat > "$T/bin/uci" <<'UCI'
#!/bin/sh
DB=${UCI_DB:?}
quiet=0
[ "${1:-}" = -q ] && { quiet=1; shift; }
cmd=${1:-}; shift 2>/dev/null || true
missing() { [ "$quiet" = 1 ] || echo "uci: Entry not found" >&2; exit 1; }
case "$cmd" in
show)
	for f in "$DB"/type.*; do
		[ -e "$f" ] || continue
		s=${f##*/type.}
		echo "wireless.$s=$(cat "$f")"
	done
	;;
get)
	# wireless.<sec>.<opt> | wireless.<sec> (its type) | <config>.<sec>.<opt> stored under its own name
	case "$1" in
	wireless.*)
		rest=${1#wireless.}
		case "$rest" in
		*.*) s=${rest%%.*}; o=${rest#*.}
			[ -f "$DB/$s.$o" ] && { cat "$DB/$s.$o"; exit 0; }
			missing ;;
		*)  [ -f "$DB/type.$rest" ] && { cat "$DB/type.$rest"; exit 0; }
			missing ;;
		esac
		;;
	*)
		[ -f "$DB/$1" ] && { cat "$DB/$1"; exit 0; }
		missing ;;
	esac
	;;
set)
	case "$1" in
	wireless.*)
		rest=${1#wireless.}
		case "$rest" in
		*.*) s=${rest%%.*}; rest=${rest#*.}; o=${rest%%=*}; v=${rest#*=}
			printf '%s' "$v" > "$DB/$s.$o"
			echo "set $s.$o=$v" >> "$UCI_DB.log" ;;
		*)  s=${rest%%=*}; v=${rest#*=}
			printf '%s' "$v" > "$DB/type.$s"
			echo "set $s=$v" >> "$UCI_DB.log" ;;
		esac
		;;
	*) name=${1%%=*}; v=${1#*=}
		printf '%s' "$v" > "$DB/$name"
		echo "set $name=$v" >> "$UCI_DB.log" ;;
	esac
	;;
delete)
	case "$1" in
	wireless.*)
		rest=${1#wireless.}
		case "$rest" in
		*.*) s=${rest%%.*}; o=${rest#*.}
			rm -f "$DB/$s.$o"
			echo "delete $s.$o" >> "$UCI_DB.log" ;;
		*)  rm -f "$DB/type.$rest" "$DB/$rest".*
			echo "delete $rest" >> "$UCI_DB.log" ;;
		esac
		;;
	*) rm -f "$DB/$1" ;;
	esac
	;;
commit)
	echo "commit $*" >> "$UCI_DB.log"
	;;
*)
	[ "$quiet" = 1 ] || echo "uci: unknown $cmd" >&2
	exit 1
	;;
esac
UCI
cat > "$T/bin/wifi" <<'WIFI'
#!/bin/sh
echo "wifi $*" >> "$UCI_DB.log"
WIFI
cat > "$T/bin/logger" <<'LOGGER'
#!/bin/sh
shift 2 2>/dev/null
echo "log $*" >> "$UCI_DB.log"
LOGGER
chmod +x "$T/bin/uci" "$T/bin/wifi" "$T/bin/logger"
export UCI_DB=$DB
PATH=$T/bin:$PATH
export PATH

# A router as OpenWrt describes it: two radios, one AP interface each. Re-seeded before every case so one
# case cannot pass on another's leftovers.
seed_db() {
	rm -f "$DB"/* "$LOG"
	printf 'wifi-device' > "$DB/type.radio0"; printf '2g' > "$DB/radio0.band"
	printf 'wifi-device' > "$DB/type.radio1"; printf '5g' > "$DB/radio1.band"
	printf 'wifi-iface' > "$DB/type.default_radio0"
	printf 'radio0' > "$DB/default_radio0.device"; printf 'ap' > "$DB/default_radio0.mode"
	printf 'old-2g' > "$DB/default_radio0.ssid"; printf 'old-key-2g' > "$DB/default_radio0.key"
	printf 'psk2' > "$DB/default_radio0.encryption"; printf '0' > "$DB/default_radio0.disabled"
	printf 'wifi-iface' > "$DB/type.default_radio1"
	printf 'radio1' > "$DB/default_radio1.device"; printf 'ap' > "$DB/default_radio1.mode"
	printf 'old-5g' > "$DB/default_radio1.ssid"; printf 'old-key-5g' > "$DB/default_radio1.key"
	printf 'psk2' > "$DB/default_radio1.encryption"; printf '0' > "$DB/default_radio1.disabled"
	: > "$LOG"
}
seed_db

pass=0
fail=0
check() { # NAME EXPECTED ACTUAL
	if [ "$2" = "$3" ]; then pass=$((pass + 1)); else
		echo "FAIL $1: expected '$2', got '$3'"
		fail=$((fail + 1))
	fi
}
val() { cat "$DB/$1" 2>/dev/null; }

PLAN=$T/plan
cat > "$PLAN" <<'PLAN'
# zte-mesh agent synchronisation plan.
rows=2
0.radio=0
0.slot=0
0.instance=DEV.WIFI.AP1
0.psk_instance=DEV.WIFI.AP1.PSK1
0.ssid=zte-agent-2g
0.key=m2-public-2g-key
0.enabled=1
0.hidden=0
0.access=0
0.beacon_type=6
1.radio=1
1.slot=0
1.instance=DEV.WIFI.AP5
1.psk_instance=DEV.WIFI.AP5.PSK1
1.ssid=zte-agent-5g
1.key=m2-public-5g-key
1.enabled=1
1.hidden=1
1.access=0
1.beacon_type=3
PLAN

seed_db
sh "$WLAN_SYNC" "$PLAN" --once >"$T/out" 2>&1
check "2.4G name" zte-agent-2g "$(val default_radio0.ssid)"
check "2.4G key" m2-public-2g-key "$(val default_radio0.key)"
check "2.4G security (beacon 6 = WPA/WPA2 mixed)" psk-mixed "$(val default_radio0.encryption)"
check "2.4G enabled" 0 "$(val default_radio0.disabled)"
check "5G name" zte-agent-5g "$(val default_radio1.ssid)"
check "5G key" m2-public-5g-key "$(val default_radio1.key)"
check "5G security (beacon 3 = WPA2)" psk2 "$(val default_radio1.encryption)"
check "5G hidden" 1 "$(val default_radio1.hidden)"
check "one commit" 1 "$(grep -c '^commit wireless$' "$LOG")"
check "one reload" 1 "$(grep -c '^wifi reload$' "$LOG")"

# Idempotent: the same plan again writes, commits and reloads nothing.
: > "$LOG"
sh "$WLAN_SYNC" "$PLAN" --once >"$T/out" 2>&1
check "second run sets nothing" 0 "$(grep -c '^set ' "$LOG")"
check "second run commits nothing" 0 "$(grep -c '^commit ' "$LOG")"
check "second run reloads nothing" 0 "$(grep -c '^wifi ' "$LOG")"

# A disabled row disables the radio's AP, an open row drops the key.
sed -e 's/^0.enabled=1$/0.enabled=0/' -e 's/^1.beacon_type=3$/1.beacon_type=0/' "$PLAN" > "$T/plan2"
seed_db
sh "$WLAN_SYNC" "$T/plan2" --once >"$T/out" 2>&1
check "disabled band" 1 "$(val default_radio0.disabled)"
check "open network security" none "$(val default_radio1.encryption)"
check "open network key removed" "" "$(val default_radio1.key)"

# An extra SSID slot is reported and skipped: the factory daemon owns those.
sed -e 's/^1.slot=0$/1.slot=2/' "$PLAN" > "$T/plan3"
seed_db
sh "$WLAN_SYNC" "$T/plan3" --once >"$T/out" 2>&1
check "slot 1 (5G) still applied" psk2 "$(val default_radio1.encryption)"
check "extra slot logged" 1 "$(grep -c 'slot 2 of radio 1 is not applied' "$LOG")"

# An unknown security mode leaves the interface alone instead of guessing.
sed -e 's/^1.beacon_type=3$/1.beacon_type=7/' "$PLAN" > "$T/plan4"
seed_db
sh "$WLAN_SYNC" "$T/plan4" --once >"$T/out" 2>&1
check "unknown mode leaves security" psk2 "$(val default_radio1.encryption)"
check "unknown mode logged" 1 "$(grep -c 'unknown security mode 7' "$LOG")"

# A missing plan is not an error: the loop waits for the service to publish one.
seed_db
sh "$WLAN_SYNC" "$T/none" --once >"$T/out" 2>&1
check "missing plan touches nothing" 0 "$(grep -c '^set ' "$LOG")"

# --- backhaul (multi_ap) ---------------------------------------------------------------------------------
# A plan as the controller actually fills it: slot 0 is the fronthaul BSS (MapAccessAttr 32) and slot 3 of
# the 2.4 GHz band is the hidden backhaul BSS (64). Only the 2.4 GHz band gets a backhaul row here, so the
# 5 GHz half also proves that nothing is invented for a band the controller did not fill in.
backhaul_on() { printf 'multi_ap' > "$DB/zte_mesh.main.backhaul"; }
backhaul_off() { rm -f "$DB/zte_mesh.main.backhaul"; }

PLAN_BH=$T/plan-bh
cat > "$PLAN_BH" <<'PLAN'
rows=3
0.radio=0
0.slot=0
0.instance=DEV.WIFI.AP1
0.ssid=zte-agent-2g
0.key=m2-public-2g-key
0.enabled=1
0.hidden=0
0.access=32
0.beacon_type=6
1.radio=0
1.slot=3
1.instance=DEV.WIFI.AP4
1.ssid=ZMESH-bh-2g
1.key=backhaul-2g-key
1.enabled=1
1.hidden=1
1.access=64
1.beacon_type=6
2.radio=1
2.slot=0
2.instance=DEV.WIFI.AP5
2.ssid=zte-agent-5g
2.key=m2-public-5g-key
2.enabled=1
2.hidden=0
2.access=32
2.beacon_type=3
PLAN

# Off (the default) is byte-for-byte the old behaviour: the backhaul row is reported, nothing is created.
seed_db
backhaul_off
sh "$WLAN_SYNC" "$PLAN_BH" --once >"$T/out" 2>&1
check "off: no backhaul section" "" "$(val type.zte_backhaul_2g)"
check "off: main BSS carries no multi_ap" "" "$(val default_radio0.multi_ap)"
check "off: backhaul row reported" 1 "$(grep -c 'backhaul mode is off; not applied' "$LOG")"
check "off: fronthaul row still applied" zte-agent-2g "$(val default_radio0.ssid)"

# On: the backhaul row becomes its own BSS, the main BSS of each band becomes the fronthaul half.
seed_db
backhaul_on
sh "$WLAN_SYNC" "$PLAN_BH" --once >"$T/out" 2>&1
check "backhaul section type" wifi-iface "$(val type.zte_backhaul_2g)"
check "backhaul radio" radio0 "$(val zte_backhaul_2g.device)"
check "backhaul mode" ap "$(val zte_backhaul_2g.mode)"
check "backhaul multi_ap" 2 "$(val zte_backhaul_2g.multi_ap)"
check "backhaul name is the controller's" ZMESH-bh-2g "$(val zte_backhaul_2g.ssid)"
check "backhaul key is the controller's" backhaul-2g-key "$(val zte_backhaul_2g.key)"
check "backhaul security (beacon 6)" psk-mixed "$(val zte_backhaul_2g.encryption)"
check "backhaul hidden" 1 "$(val zte_backhaul_2g.hidden)"
check "backhaul enabled" 0 "$(val zte_backhaul_2g.disabled)"
check "fronthaul 2.4G marked multi_ap 1" 1 "$(val default_radio0.multi_ap)"
check "fronthaul 5G marked multi_ap 1" 1 "$(val default_radio1.multi_ap)"
check "no 5G backhaul row, no 5G section" "" "$(val type.zte_backhaul_5g)"
check "main SSID untouched by the backhaul row" zte-agent-2g "$(val default_radio0.ssid)"
check "one commit for the whole plan" 1 "$(grep -c '^commit wireless$' "$LOG")"

# Idempotent in this mode too.
: > "$LOG"
sh "$WLAN_SYNC" "$PLAN_BH" --once >"$T/out" 2>&1
check "second backhaul run sets nothing" 0 "$(grep -c '^set ' "$LOG")"
check "second backhaul run commits nothing" 0 "$(grep -c '^commit ' "$LOG")"
check "second backhaul run reloads nothing" 0 "$(grep -c '^wifi ' "$LOG")"

# Switching back to off undoes both halves, and says so.
backhaul_off
: > "$LOG"
sh "$WLAN_SYNC" "$PLAN_BH" --once >"$T/out" 2>&1
check "off again: section removed" "" "$(val type.zte_backhaul_2g)"
check "off again: fronthaul flag cleared" "" "$(val default_radio0.multi_ap)"
check "off again: removal logged" 1 "$(grep -c 'removed zte_backhaul_2g' "$LOG")"
check "off again: flag clearing logged" 1 "$(grep -c 'cleared multi_ap on default_radio0' "$LOG")"

# A multi_ap value this script never writes is somebody else's: off mode leaves it alone.
seed_db
printf '3' > "$DB/default_radio0.multi_ap"
backhaul_off
sh "$WLAN_SYNC" "$PLAN_BH" --once >"$T/out" 2>&1
check "off: a foreign multi_ap 3 is kept" 3 "$(val default_radio0.multi_ap)"

# An open backhaul drops the key instead of writing an empty one, and an unknown mode is not guessed.
sed -e 's/^1.beacon_type=6$/1.beacon_type=0/' "$PLAN_BH" > "$T/plan-bh2"
seed_db
backhaul_on
sh "$WLAN_SYNC" "$T/plan-bh2" --once >"$T/out" 2>&1
check "open backhaul security" none "$(val zte_backhaul_2g.encryption)"
check "open backhaul key removed" "" "$(val zte_backhaul_2g.key)"

sed -e 's/^1.beacon_type=6$/1.beacon_type=9/' "$PLAN_BH" > "$T/plan-bh3"
seed_db
backhaul_on
sh "$WLAN_SYNC" "$T/plan-bh3" --once >"$T/out" 2>&1
check "unknown backhaul mode creates nothing" "" "$(val type.zte_backhaul_2g)"
check "unknown backhaul mode logged" 1 "$(grep -c 'backhaul on 2g: unknown security mode 9' "$LOG")"

# A plan without the access field (an older service build) still marks the fronthaul but invents no backhaul.
seed_db
backhaul_on
sh "$WLAN_SYNC" "$PLAN" --once >"$T/out" 2>&1
check "old plan: fronthaul still marked" 1 "$(val default_radio0.multi_ap)"
check "old plan: no backhaul section invented" "" "$(val type.zte_backhaul_2g)"


# Every run leaves a status file next to the plan (wlan-sync.status): the sandboxed service and the page cannot see
# what happens outside the sandbox, and without it a failed apply is only a line in the system log.
status_val() { sed -n "s/^$1=//p" "$T/wlan-sync.status" 2>/dev/null | head -n 1; }
seed_db
sh "$WLAN_SYNC" "$PLAN" --once >"$T/out" 2>&1
check "status: result" ok "$(status_val result)"
check "status: an apply counts as changed" 1 "$(status_val changed)"
check "status: both bands named" "2g 5g" "$(status_val bands)"
check "status: epoch is a number" 1 "$(status_val epoch | grep -c '^[0-9][0-9]*$')"

# The same plan again changes nothing: the bands field must be empty, not stale.
sh "$WLAN_SYNC" "$PLAN" --once >"$T/out" 2>&1
check "status: a no-op run reports no change" 0 "$(status_val changed)"
check "status: a no-op run names no band" "" "$(status_val bands)"
check "status: still ok" ok "$(status_val result)"

echo "wlan-sync: $pass passed, $fail failed"
[ "$fail" = 0 ]
