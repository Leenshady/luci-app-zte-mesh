# EXAMPLE test driver kept for reference. It was written against the scratch layout of the session (paths in D/C below),
# a real-child config file ("real-restore.config": the child original Wi-Fi values, plaintext keys, NOT in the repo) and a running
# dev-service-smoke.py. Adapt the paths before use. Read TESTING.md first: these push real configuration to a real child.
import subprocess, time, re
D='/tmp/claude-1000/-home-cnjn-code/c9a23609-8395-4465-b247-7d86616dc398/scratchpad'
C=D+'/child'
ssh=[D+'/bin/ssh','root@192.168.174.5']
base=open(C+'/real-restore.config').read()
base=re.sub(r'(?m)^version_mode=.*\n','',base)+'version_mode=0\n'
t0=time.time()
def log(m): print('T+%03d %s'%(time.time()-t0,m),flush=True)
def sh(cmd,inp=None): return subprocess.run(ssh+[cmd],input=inp,capture_output=True,text=True,timeout=40).stdout
def tn(script,t=40): return subprocess.run(['python3',D+'/bin/ztetn.py','--timeout',str(t)],stdin=open(C+'/'+script),capture_output=True,text=True,timeout=t+40).stdout
def readback(tag):
    out=tn('rd-acl.sh')
    head=out.split('--- policy')[0]
    pol=out.split('--- policy')[-1]
    rows=re.findall(r'<DM name="(\w+)" val="([^"]*)"',head)
    acl=[];cur={}
    for k,v in rows:
        if k=='ViewName' and cur: acl.append(cur);cur={}
        cur[k]=v
    if cur: acl.append(cur)
    cnt=re.findall(r'RowCount="(\d+)"',head)
    pols=[];vn=None
    for k,v in re.findall(r'<DM name="(\w+)" val="([^"]*)"',pol):
        if k=='ViewName': vn=v[-3:]
        elif k=='ACLPolicy': pols.append('%s=%s'%(vn,v))
    log('READBACK %s: AclCfg rows=%s %s | policy %s'%(tag,cnt,[(r.get('Name'),r.get('Interface'),r.get('MACAddress')) for r in acl if r.get('Name')],' '.join(pols[:8])))
def setcfg(extra,name):
    sh('umask 077 && cat > /run/zte-mesh/config.new && mv /run/zte-mesh/config.new /run/zte-mesh/config',base+extra); log('config -> '+name)
def svc():
    log('service: '+sh('grep -a "acl_update\\|acl_rules" /tmp/zte-mesh-svc/service.log | tail -3').strip().replace('\n',' | '))
readback('baseline')
while subprocess.run(ssh+['test -f /run/zte-mesh/state.json'],capture_output=True).returncode: time.sleep(2)
log('controller up (acl.1 configured)'); time.sleep(8)
tn('k4.sh',30); log('child multiapd killed'); time.sleep(55)
svc(); readback('A1 after onboarding with acl.1')
setcfg('acl.1.mac=02:aa:bb:cc:dd:01\nacl.1.name=zte-test\nacl.2.mac=02:aa:bb:cc:dd:02\nacl.2.name=zte-test2\nacl.2.radio=0\n','A2 add acl.2 (radio0 only)'); time.sleep(30)
svc(); readback('A2')
setcfg('','A3 remove all rules'); time.sleep(30)
svc(); readback('A3')
setcfg('radio0.acl_policy=disabled\n','A4 radio0 policy disabled'); time.sleep(30)
svc(); readback('A4')
setcfg('','A5 policy back to default'); time.sleep(30)
svc(); readback('A5')
log('done')
