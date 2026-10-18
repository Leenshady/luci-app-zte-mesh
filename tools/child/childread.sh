#!/bin/sh
# childread.sh: compact WLANCfg readback of the Z506 child, one line per AP row (run it on the mac).
# The telnet password comes only from the environment (ZTE_TN_PASS); nothing is stored. The DB dump the
# child writes for this (/var/tmp/dbtable, it holds the keys unmasked) is deleted on the child right away,
# and only the listed non-secret fields are printed. Compare with the baseline in docs/REAL_CHILD.md.
set -eu
: "${ZTE_TN_PASS:?set ZTE_TN_PASS}"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CMD='sendcmd 1 DB pf WLANCfg >/dev/null 2>&1; grep -E "ViewName|InstExist|Enable|ESSID|BeaconType|VapIsolationEnable|GuestSsid|MapAccessAttr|ACLPolicy" /var/tmp/dbtable; rm -f /var/tmp/dbtable'
python3 "$HERE/ztetn.py" --host "${CHILD:-192.168.5.216}" --port 23 --timeout 40 "$CMD" 2>&1 | tr -d '\r' | python3 -c '
import re, sys
cur = None
rows = {}
keys = ("InstExist", "Enable", "ESSID", "BeaconType", "ESSIDHideEnable", "VapIsolationEnable", "GuestSsid", "MapAccessAttr", "ACLPolicy")
for line in sys.stdin:
    m = re.search(r"name=\"(\w+)\" val=\"([^\"]*)\"", line)
    if not m:
        continue
    k, v = m.groups()
    if k == "ViewName" and v.startswith("DEV.WIFI.AP"):
        cur = v[-3:]
        rows[cur] = {}
    elif cur and k in keys:
        rows[cur][k] = v
for ap in sorted(rows):
    print(ap, " ".join("%s=%s" % (k, rows[ap].get(k, "?")) for k in keys))
'
