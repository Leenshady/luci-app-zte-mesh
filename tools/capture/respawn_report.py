#!/usr/bin/env python3
"""respawn_report.py PCAP EPOCH [EPOCH...]: what the wire shows after each daemon respawn (REPLAY_RESULTS section 12).

EPOCH is the router epoch printed by the service in 'daemon_respawn ... epoch=N'. For each respawn the report lists, relative to it:
first controller frame, Renews (time, count), WSC M1/M2, the 0200fe module message (VendorSpecific len 84), the re-join (first TopoDiscovery
from the child after the flap), the first PolicyConfigReq with its 60[2] (P1905ComCfg.Enable, MarketConfEn), and how many child TopoResponse
frames had no 1905Nbr in between. Uses cmdu.py (same directory) for the frame list.
"""
import re, subprocess, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
LINE = re.compile(r"^\s*\d+ (\d\d):(\d\d):(\d\d)\.(\d+) (A>M\*?|M>A\*?)\s+(\S+)\s+mid=\S+ frag=(\d+) fl=\S+ len=(\d+)\s*(.*)$")


def frames(pcap):
    out = subprocess.run([sys.executable, os.path.join(HERE, "cmdu.py"), pcap, "--vendor"], capture_output=True, text=True, check=True).stdout
    res = []
    for line in out.splitlines():
        m = LINE.match(line)
        if m:
            h, mi, s, ms, d, name, frag, ln, rest = m.groups()
            res.append({"t": int(h) * 3600 + int(mi) * 60 + int(s) + int(ms) / 1000.0, "dir": d, "name": name, "frag": int(frag),
                        "len": int(ln), "tlv": rest, "vendor": ""})
        elif line.strip().startswith("vendor:") and res:
            res[-1]["vendor"] += line.strip()
    return res


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    fr = frames(sys.argv[1])
    epochs = [int(x) for x in sys.argv[2:]]
    for k, e in enumerate(epochs):
        t0 = e % 86400
        t1 = (epochs[k + 1] % 86400) if k + 1 < len(epochs) else 1e9
        win = [f for f in fr if t0 - 1 <= f["t"] < t1]
        print("== respawn %d at epoch %d (capture clock %02d:%02d:%02d)" % (k + 1, e, t0 // 3600, t0 // 60 % 60, t0 % 60))
        rel = lambda f: "%+7.2f s" % (f["t"] - t0)
        for f in win[:1]:
            print("  first frame      %s  %s %s" % (rel(f), f["dir"], f["name"]))
        renew = [f for f in win if f["name"] == "APAutoconfRenew"]
        print("  Renews           %d  %s" % (len(renew), ", ".join(rel(f) for f in renew[:6])))
        m1 = [f for f in win if f["name"] == "APAutoconfWSC" and f["dir"].startswith("A")]
        m2 = [f for f in win if f["name"] == "APAutoconfWSC" and f["dir"].startswith("M") and f["frag"] == 0]
        print("  WSC M1 / M2      %d / %d  M1 at %s" % (len(m1), len(m2), ", ".join(rel(f) for f in m1[:4])))
        nonbr = [f for f in win if f["name"] == "TopoResponse" and f["dir"] == "M>A" and "1905Nbr" not in f["tlv"]]
        print("  TopoResponse without 1905Nbr: %d (first %s, last %s)" % (len(nonbr), rel(nonbr[0]) if nonbr else "-", rel(nonbr[-1]) if nonbr else "-"))
        rejoin = [f for f in win if f["name"] == "TopoDiscovery" and f["dir"].startswith("A") and f["t"] - t0 > 5 and
                  any(g["name"] == "PolicyConfigReq" and 0 <= g["t"] - f["t"] < 2 for g in win)]
        print("  re-join (child TopoDiscovery -> PolicyConfigReq)  %s" % (rel(rejoin[0]) if rejoin else "none seen"))
        pcr = [f for f in win if f["name"] == "PolicyConfigReq"]
        for f in pcr[:3]:
            m = re.search(r"60\[2\]=([0-9a-f]{4})", f["vendor"])
            print("  PolicyConfigReq  %s len=%d 60[2]=%s" % (rel(f), f["len"], m.group(1) if m else "-"))
        mlo = [f for f in win if f["name"] == "VendorSpecific" and f["len"] == 84]
        print("  0200fe (len 84)  %s" % (", ".join(rel(f) for f in mlo[:4]) or "none"))


main()
