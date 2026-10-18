"""snapdb.py: parse `sendcmd 1 DB p` dumps into {table: [ {field: (val, isSave)} per row ]}; diff two dumps."""
import re, sys

TBL = re.compile(r'<Tbl name="([^"]+)" RowCount="(\d+)"')
ROW = re.compile(r'<Row No="(\d+)"')
DM = re.compile(r'<DM name="([^"]*)" val="(.*?)" isSave="(\d)"')

def parse(path):
    db, cur, row = {}, None, None
    for line in open(path, errors="replace"):
        m = TBL.search(line)
        if m:
            cur = db.setdefault(m.group(1), []); row = None; continue
        m = ROW.search(line)
        if m and cur is not None:
            row = {}; cur.append(row); continue
        m = DM.search(line)
        if m and row is not None:
            row[m.group(1)] = (m.group(2), m.group(3))
    return db

def diff(a, b):
    out = []
    for t in sorted(set(a) | set(b)):
        ra, rb = a.get(t, []), b.get(t, [])
        if len(ra) != len(rb):
            out.append((t, "RowCount", f"{len(ra)} -> {len(rb)}", None))
        for i in range(min(len(ra), len(rb))):
            for f in sorted(set(ra[i]) | set(rb[i])):
                va, vb = ra[i].get(f), rb[i].get(f)
                if va != vb:
                    out.append((f"{t}[{i}]", f, f"{va[0] if va else None!r} -> {vb[0] if vb else None!r}", (vb or va)[1]))
    return out

if __name__ == "__main__":
    if len(sys.argv) == 3:
        for t, f, ch, sv in diff(parse(sys.argv[1]), parse(sys.argv[2])):
            print(f"{t}.{f}: {ch}" + (f"  (save={sv})" if sv is not None else ""))
