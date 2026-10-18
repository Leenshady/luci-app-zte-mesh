# ACL state: DB rules + per-BSS policy, then what the Ralink-style driver really holds (read-only)
sendcmd 1 DB p AclCfg 2>&1 | grep -E "RowCount|ViewName|Name|Interface|MACAddress|ACLCfgPolicy"
sendcmd 1 DB p WLANCfg 2>&1 | grep -E "\"ViewName\"|\"ACLPolicy\""
# driver list goes to the kernel log: clear, trigger, read (busybox dmesg is needed: copy busybox to /var/tmp first)
/var/tmp/busybox dmesg -c > /dev/null 2>&1
for i in wlan0 wlan5g0; do iwpriv $i set ACLShowAll=1 > /dev/null 2>&1; done
/var/tmp/busybox dmesg | tail -30
