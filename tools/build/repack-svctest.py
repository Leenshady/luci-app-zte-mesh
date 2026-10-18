import tarfile,hashlib,io,sys,shutil
src='zte-mesh-svctest.tar.gz'; new={'root/lib/libzte_controller_profile.so':'/tmp/svctest-repack/libzte_controller_profile.so','root/bin/db-engine-probe':'/tmp/svctest-repack/db-engine-probe','root/bin/zte-meshctl':'/tmp/svctest-repack/zte-meshctl'}
tin=tarfile.open(src); out=io.BytesIO(); tout=tarfile.open(fileobj=out,mode='w:gz')
sums=None
for m in tin.getmembers():
    if m.name.endswith('SHA256SUMS'): sums=m; continue
for m in tin.getmembers():
    if m is sums: continue
    if m.name in new:
        d=open(new[m.name],'rb').read(); m.size=len(d); tout.addfile(m,io.BytesIO(d))
    elif m.isfile(): tout.addfile(m,tin.extractfile(m))
    else: tout.addfile(m)
# regenerate sums
tin2=tarfile.open(src)
lines=[]
for m in tin2.getmembers():
    if m is None or not m.isfile() or m.name.endswith('SHA256SUMS'): continue
    d=open(new[m.name],'rb').read() if m.name in new else tin2.extractfile(m).read()
    lines.append('%s  %s\n'%(hashlib.sha256(d).hexdigest(),m.name))
print(tin.extractfile(sums).read()[:200]); 
ti=tarfile.TarInfo(sums.name); b=''.join(lines).encode(); ti.size=len(b); ti.mode=sums.mode; ti.mtime=sums.mtime
tout.addfile(ti,io.BytesIO(b)); tout.close()
open(src+'.new','wb').write(out.getvalue())
