# EXAMPLE test driver kept for reference. It was written against the scratch layout of the session (paths in D/C below),
# a real-child config file ("real-restore.config": the child original Wi-Fi values, plaintext keys, NOT in the repo) and a running
# dev-service-smoke.py. Adapt the paths before use. Read TESTING.md first: these push real configuration to a real child.
import subprocess, time, re, os
D='/tmp/claude-1000/-home-cnjn-code/c9a23609-8395-4465-b247-7d86616dc398/scratchpad'
C=D+'/child'
ssh=[D+'/bin/ssh','root@192.168.174.5']
base=open(C+'/real-restore.config').read()
base=re.sub(r'(?m)^version_mode=.*\n','',base)+'version_mode=0\n'
t0=time.time()
def log(m): print('T+%03d %s'%(time.time()-t0,m),flush=True)
def sh(cmd,inp=None): return subprocess.run(ssh+[cmd],input=inp,capture_output=True,text=True,timeout=40).stdout
def tn(script,t=40): return subprocess.run(['python3',D+'/bin/ztetn.py','--timeout',str(t)],stdin=open(C+'/'+script),capture_output=True,text=True,timeout=t+40).stdout
def acl_state(tag):
    out=tn('rd-acl.sh'); head=out.split('--- policy')[0]
    rows=re.findall(r'<DM name="(\w+)" val="([^"]*)"',head); acl=[];cur={}
    for k,v in rows:
        if k=='ViewName' and cur: acl.append(cur);cur={}
        cur[k]=v
    if cur: acl.append(cur)
    log('%s: AclCfg rows=%s %s'%(tag,re.findall(r'RowCount="(\d+)"',head),[(r.get('Interface'),r.get('MACAddress')) for r in acl if r.get('Name')]))
def sta_state():
    s=tn('sta.sh'); macs=re.findall(r'^([0-9a-f]{2}(?::[0-9a-f]{2}){5})\s',s,re.M)
    fdb=sh('bridge fdb show br br-lan | grep -v -e permanent -e " self " | grep lan2 | cut -d" " -f1').split()
    return macs,fdb
while subprocess.run(ssh+['test -f /run/zte-mesh/state.json'],capture_output=True).returncode: time.sleep(2)
log('controller up with ban rule for the OnePlus'); time.sleep(8)
tn('k4.sh',30); log('child multiapd killed (re-onboard, ACL pushed in the vendor phase)'); time.sleep(55)
acl_state('RULE ACTIVE')
last=None
while not os.path.exists(C+'/release.flag') and time.time()-t0<1500:
    macs,fdb=sta_state()
    cur=(tuple(macs),tuple(fdb))
    if cur!=last: log('child stations %s | lan2 fdb %s'%(macs,fdb)); last=cur
    time.sleep(8)
log('release requested -> removing the rule')
sh('umask 077 && cat > /run/zte-mesh/config.new && mv /run/zte-mesh/config.new /run/zte-mesh/config',base); time.sleep(35)
acl_state('AFTER REMOVAL')
for i in range(6):
    macs,fdb=sta_state(); log('child stations %s | lan2 fdb %s'%(macs,fdb)); time.sleep(10)
subprocess.run(['pkill','-INT','-f','dev-service-smoke.py --seconds 1700'])
log('done')
