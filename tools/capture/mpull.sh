#!/bin/bash
# mpull.sh: pull the vendor-side capture (/DPITMP/mw) here and print the DB changes between consecutive snapshots.
SP=$(dirname "$0"); export ZTE_TN_PASS
$SP/vt.sh 'cd /DPITMP/mw && tar cf /DPITMP/mw/out.tar c_*.gz watch.log 2>/dev/null; ls -la out.tar' 20 >/dev/null
rm -rf $SP/cap && mkdir -p $SP/cap
$SP/vfetch.sh /DPITMP/mw/out.tar $SP/cap/out.tar >/dev/null
(cd $SP/cap && tar xf out.tar 2>/dev/null; gunzip -f c_*.gz 2>/dev/null)
python3 - "$SP" <<'PY'
import sys, glob, os
sp = sys.argv[1]; sys.path.insert(0, sp)
from snapdb import parse, diff
snaps = [sp + "/snap/s000.txt"] + sorted([f for f in glob.glob(sp + "/cap/c_*") if not f.endswith(".gz")])
import re
NOISE = re.compile(r'^(Laninfo|AccessDev|DHCP4s|DHCPSHost|InterfaceStatus|Brport|AdevGroup|HLT|WanAddr|WanDetect|DNS|QOSStats|OnlineTime|UPnP|NatPt)')
prev = parse(snaps[0])
for p in snaps[1:]:
    cur = parse(p)
    lines = []; hid = 0
    for t, f, ch, sv in diff(prev, cur):
        if NOISE.match(t): hid += 1; continue
        lines.append(f"   {t}.{f}: {ch}" + ("" if sv in (None, "1") else "   [runtime, not saved]"))
    if lines:
        print("== " + os.path.basename(p)[2:]); print("\n".join(lines))
    prev = cur
if len(snaps) == 1: print("(no changes recorded yet)")
PY
