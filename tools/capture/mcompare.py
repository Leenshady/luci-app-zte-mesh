#!/usr/bin/env python3
"""mcompare.py A.pcap B.pcap [--a-win T0 T1] [--b-win T0 T1] [--drop-periodic] [--summary]

Compare two IEEE 1905 / Multi-AP captures at TLV level (vendor controller vs shim, or vendor vs vendor to calibrate noise).
Windows are HH:MM:SS(.fff) in the capture's own UTC clock; omit for the whole file.
Normalisation: drop MIDs, timestamps, WSC/crypto bytes, volatile metric/device values; keep direction, CMDU type, TLV type+length,
M2 fragment lengths, config-like values (policy/vendor sub-TLVs), fragment ids.
"""
import struct, sys, time, difflib, collections

CM = {0: "TopoDiscovery", 1: "TopoNotification", 2: "TopoQuery", 3: "TopoResponse", 4: "VendorSpecific", 5: "LinkMetricQuery",
      6: "LinkMetricResponse", 7: "APAutoconfSearch", 8: "APAutoconfResponse", 9: "APAutoconfWSC", 10: "APAutoconfRenew",
      13: "HigherLayerQuery", 14: "HigherLayerResponse", 0x8000: "Ack", 0x8001: "APCapQuery", 0x8002: "APCapReport",
      0x8003: "PolicyConfigReq", 0x8004: "ChanPrefQuery", 0x8005: "ChanPrefReport", 0x8006: "ChanSelReq", 0x8007: "ChanSelResp",
      0x8008: "OperChanReport", 0x8009: "ClientCapQuery", 0x800a: "ClientCapReport", 0x800b: "APMetricsQuery", 0x800c: "APMetricsResp",
      0x800d: "AssocSTALinkQuery", 0x800e: "AssocSTALinkResp", 0x8012: "BeaconMetricsResp", 0x8013: "CombinedInfraMetrics",
      0x8018: "HigherLayerData"}
TL = {0: "EOM", 1: "ALMAC", 2: "MAC", 3: "DevInfo", 4: "BridgeCap", 6: "Non1905Nbr", 7: "1905Nbr", 8: "LinkMetricQ", 9: "TxLinkMetric",
      10: "RxLinkMetric", 0x0b: "Vendor", 0x0d: "SearchedRole", 0x0e: "AutoconfBand", 0x0f: "SupportedRole", 0x10: "SupportedBand",
      0x11: "WSC", 0x15: "DevIdent", 0x17: "IPv4", 0x18: "IPv6", 0x1a: "Profile1905", 0x80: "SupportedSvc", 0x81: "SearchedSvc",
      0x82: "APRadioId", 0x83: "APOperBSS", 0x84: "AssocClients", 0x85: "APRadioCaps", 0x86: "APHTCaps", 0x87: "APVHTCaps",
      0x88: "APHECaps", 0x89: "SteeringPolicy", 0x8a: "MetricRptPolicy", 0x8b: "ChanPref", 0x8c: "RadioOpRestr", 0x8f: "OperChanReport",
      0x90: "ClientInfo", 0x91: "ClientCapReport", 0x92: "ClientAssocEvent", 0x93: "APMetricQuery", 0x94: "APMetrics",
      0x95: "STAMACType", 0x96: "AssocSTALink", 0xa1: "APCapability", 0xa2: "AssocSTATraffic", 0xb3: "MultiAPProfile"}
PERIODIC = {"APMetricsQuery", "APMetricsResp", "AssocSTALinkQuery", "AssocSTALinkResp", "LinkMetricQuery", "LinkMetricResponse",
            "TopoDiscovery"}
KEEP_VALUE = {0x0d, 0x0e, 0x0f, 0x10, 0x1a, 0x80, 0x81, 0x8a, 0xb3, 0xa1}   # config-like standard TLVs: keep value
CONFIG_HDRS = ("020007", "010100", "010101", "010102", "010000", "010001", "010002")   # vendor message families whose sub-TLVs are config
CHILD_PREFIX = bytes.fromhex("d4b7090d77")


def tlvs(b):
    o = 0
    while o + 3 <= len(b):
        t = b[o]; l = struct.unpack(">H", b[o + 1:o + 3])[0]; v = b[o + 3:o + 3 + l]; o += 3 + l
        yield t, v
        if t == 0:
            break


def vendor_desc(v):
    """normalised description of a vendor-specific TLV value"""
    oui = v[:3].hex()
    if oui != "0019c6" or len(v) < 6:
        return "V[%s:%d]" % (oui, len(v))
    hdr = v[3:6].hex()
    body = v[6:]
    if hdr.endswith("fe"):                                   # cross-device module message
        try:
            name = body[2:2 + body[1]].split(b"\0")[0].decode()
        except Exception:
            name = "?"
        return "V[fwd %s val=%s]" % (name, body[-4:].hex())
    if hdr in CONFIG_HDRS:
        subs = []; o = 0
        while o + 3 <= len(body):
            st = body[o]; sl = struct.unpack(">H", body[o + 1:o + 3])[0]
            subs.append("%02x=%s" % (st, body[o + 3:o + 3 + sl].hex())); o += 3 + sl
        return "V[%s %s]" % (hdr, " ".join(subs))
    return "V[%s len=%d]" % (hdr, len(body))                 # e.g. 020009 device info: contents are identity, compare length only


def describe(frame):
    ver, res, mt, mid, fid, flags = struct.unpack(">BBHHBB", frame[14:22])
    parts = []
    for t, v in tlvs(frame[22:]):
        if t == 0:
            continue
        nm = TL.get(t, "%#04x" % t)
        if t == 0x0b:
            parts.append(vendor_desc(v))
        elif t in KEEP_VALUE:
            parts.append("%s=%s" % (nm, v.hex()))
        elif t in (1, 2):
            parts.append(nm)
        else:
            parts.append("%s(%d)" % (nm, len(v)))
    return mt, fid, flags, parts


def load(path, win):
    d = open(path, "rb").read(); o = 24; out = []
    t0, t1 = win if win else (None, None)
    def hms(s):
        h, m, sec = s.split(":"); return int(h) * 3600 + int(m) * 60 + float(sec)
    lo = hms(t0) if t0 else -1; hi = hms(t1) if t1 else 1e18
    while o + 16 <= len(d):
        ts, tu, cl, ol = struct.unpack("<IIII", d[o:o + 16]); o += 16; fr = d[o:o + cl]; o += cl
        if len(fr) < 22:
            continue
        sec = ts % 86400 + tu / 1e6
        if not (lo <= sec <= hi):
            continue
        mt, fid, flags, parts = describe(fr)
        direction = "A>M" if fr[6:11] == CHILD_PREFIX else "M>A"
        if fr[:6] == bytes.fromhex("0180c2000013"):
            direction += "*"
        out.append({"t": sec, "dir": direction, "type": CM.get(mt, "%#06x" % mt), "frag": fid, "last": bool(flags & 0x80), "len": cl,
                    "parts": parts})
    return out


def sig(r):
    extra = ""
    if r["type"] == "APAutoconfWSC":
        extra = " frag%d len=%d" % (r["frag"], r["len"])
    return "%s %s%s %s" % (r["dir"], r["type"], extra, " ".join(r["parts"]))


def main():
    a = sys.argv
    args = [x for x in a[1:] if not x.startswith("--")]
    def win(flag):
        if flag in a:
            i = a.index(flag); return (a[i + 1], a[i + 2])
        return None
    A = load(args[0], win("--a-win")); B = load(args[1], win("--b-win"))
    if "--drop-periodic" in a:
        A = [r for r in A if r["type"] not in PERIODIC]; B = [r for r in B if r["type"] not in PERIODIC]
    sa = [sig(r) for r in A]; sb = [sig(r) for r in B]
    print("A: %d frames   B: %d frames" % (len(A), len(B)))
    ca = collections.Counter((r["dir"], r["type"]) for r in A); cb = collections.Counter((r["dir"], r["type"]) for r in B)
    print("\n-- message counts (dir type: A / B)")
    for k in sorted(set(ca) | set(cb)):
        mark = "" if ca[k] == cb[k] else "   <-- differs"
        print("  %-4s %-22s %4d / %-4d%s" % (k[0], k[1], ca[k], cb[k], mark))
    if "--summary" in a:
        return
    if "--multiset" in a:
        wa = collections.Counter(sa); wb = collections.Counter(sb)
        print("\n-- multiset diff (order ignored; A=vendor, B=other). count A/B")
        for k in sorted(set(wa) | set(wb), key=lambda x: (x.split(" ")[1] if " " in x else x, x)):
            if wa[k] != wb[k]:
                print("   %2d/%-2d %s" % (wa[k], wb[k], k[:240]))
        return
    print("\n-- aligned diff (A=-, B=+; equal runs collapsed)")
    sm = difflib.SequenceMatcher(None, sa, sb, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            print("   = %d identical message(s)" % (i2 - i1)); continue
        for s in sa[i1:i2]: print("   - " + s[:230])
        for s in sb[j1:j2]: print("   + " + s[:230])


main()
