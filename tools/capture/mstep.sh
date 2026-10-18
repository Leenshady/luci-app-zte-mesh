#!/bin/bash
# mstep.sh HH:MM:SS: what the vendor did since that vendor-UTC time: DB changes, non-periodic 1905 frames (+vendor sub-TLVs), key log lines
SP=$(dirname "$0"); export ZTE_TN_PASS; SINCE=$1
$SP/vt.sh 'cd /DPITMP/mw; cp eth4.pcap cur4.pcap; cp eth3.pcap cur3.pcap; cp eth2.pcap cur2.pcap; cp log.txt curlog.txt; tar cf out5.tar cur4.pcap cur3.pcap cur2.pcap curlog.txt' 30 >/dev/null
rm -rf $SP/step; mkdir -p $SP/step; $SP/vfetch.sh /DPITMP/mw/out5.tar $SP/step/out5.tar >/dev/null; (cd $SP/step && tar xf out5.tar)
echo "##### DB changes since $SINCE"; $SP/mpull.sh 2>&1 | awk -v s="${SINCE//:/}" '/^== /{split($2,a,"-"); p=(a[2]>=s)} p'
echo "##### 1905 frames since $SINCE (periodic metrics/discovery/acks hidden)"
for i in 4 3 2; do python3 -I $SP/cmdu.py $SP/step/cur$i.pcap --since $SINCE --quiet --vendor 2>/dev/null | sed "s/^/eth$i /"; done | cut -c1-400
echo "##### key log lines since $SINCE (log clock = vendor UTC - 11:02:46)"
sed 's/\x1b\[[0-9;]*m//g' $SP/step/curlog.txt | grep -E "web set|dbAPISave|SlaveRoamThres|AGENT RECV: (ENTER|EXIT)|ASYNMSG RECV: ENTER (event )?[a1-9][0-9a-f]{3,4}[, ]|Renew|renew|master_update|Vendor|vendor|\[acd\]\[Notice\]|\[cooperator|wlan_handle_top|OBJ_" | grep -vE "event 9092|a94f|not catch" | python3 -c '
import sys
h,m,s=map(int,sys.argv[1].split(":")); t0=(h*3600+m*60+s-(11*3600+2*60+46))%86400
for l in sys.stdin:
    try: hh,mm,ss=map(int,l[:8].split(":"))
    except: continue
    if (hh*3600+mm*60+ss)>=t0: print(l.rstrip()[:220])
' "$SINCE" | head -${2:-40}
