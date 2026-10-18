# Read-only snapshot of the child's mesh state. Run with:  python3 ztetn.py < child-state.sh
# (uses only tools the Z506 ships: grep tr cut sed awk pgrep; /var/tmp/busybox is wiped by a reboot)
sendcmd 1 DB p MultiAPCfg 2>&1 | grep -E 'DM name' | tr -s ' ' | tr '\n' ' '
echo
sendcmd 1 DB p P1905ComCfg 2>&1 | grep -E 'Tbl|DM name' | tr -s ' ' | tr '\n' ' '
echo
sendcmd multiapd.map.slave dev 2>&1 | awk 'NR<=6 || /Dev\[|uMode|eDevState|CurChannel|CurTxpower|rssithr/'
ps | grep -E "multiapd|cspd" | grep -v grep
