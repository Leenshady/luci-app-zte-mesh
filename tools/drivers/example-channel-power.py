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
def tn(script,t=40):
    r=subprocess.run(['python3',D+'/bin/ztetn.py','--timeout',str(t)],stdin=open(C+'/'+script),capture_output=True,text=True,timeout=t+40)
    return r.stdout
def readback(tag):
    out=tn('rd-ch.sh')
    rows=re.findall(r'name="(\w+)" val="([^"]*)"',out)
    db=[]; cur=None
    for k,v in rows:
        if k=='ViewName': cur=v[-3:]
        elif k in('Channel','AutoChannelEnabled','TxPower'): db.append('%s.%s=%s'%(cur,{'Channel':'ch','AutoChannelEnabled':'auto','TxPower':'pwr'}[k],v))
    ch=re.findall(r'Channel=(\d+)',out); cp=re.findall(r'CurTxpower:\s+(\d+)',out)
    log('READBACK %s: DB %s | iw ch %s | CurTxpower %s'%(tag,' '.join(db),ch,cp))
def setcfg(extra,name):
    r=subprocess.run(ssh+['umask 077 && cat > /run/zte-mesh/config.new && mv /run/zte-mesh/config.new /run/zte-mesh/config'],input=base+extra,capture_output=True,text=True,timeout=30)
    log('config -> %s rc=%d'%(name,r.returncode))
def svc():
    r=subprocess.run(ssh+['grep -a "radio_select" /tmp/zte-mesh-svc/service.log | tail -3'],capture_output=True,text=True,timeout=30)
    log('service: '+r.stdout.strip().replace('\n',' | '))
while subprocess.run(ssh+['test -f /run/zte-mesh/state.json'],capture_output=True).returncode: time.sleep(2)
log('controller up (config has radio0.channel=6)')
time.sleep(36); svc(); readback('P1 2.4G ch6')
setcfg('radio0.channel=6\nradio0.tx_power=15\n','P2 2.4G ch6 + 15 dBm'); time.sleep(26); svc(); readback('P2')
setcfg('radio0.channel=1\nradio0.tx_power=20\n','P3 2.4G ch1 + 20 dBm (restore step 1)'); time.sleep(26); svc(); readback('P3')
setcfg('radio0.channel=0\nradio0.tx_power=20\n','P4 2.4G auto + 20 dBm (restore step 2)'); time.sleep(26); svc(); readback('P4')
setcfg('radio1.channel=44\nradio1.tx_power=20\n','P5 5G ch44 + 20 dBm'); time.sleep(30); svc(); readback('P5')
setcfg('radio1.channel=36\nradio1.tx_power=27\n','P6 5G ch36 + 27 dBm (restore step 1)'); time.sleep(30); svc(); readback('P6')
setcfg('radio1.channel=0\nradio1.tx_power=27\n','P7 5G auto + 27 dBm (restore step 2)'); time.sleep(30); svc(); readback('P7')
log('done')
