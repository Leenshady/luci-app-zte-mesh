# Read cspd's g_TriggerSelectFlag block (.bss 0x203370, non-PIE EXEC; Z506 V1.0.0.2B2.8000 only).
# Word 0: bit0 = br0 IPv4 up, bit1 = ACD-up notified while Mode==auto; both set (==3) -> cspd calls CfIeee1905RoleElect.
# Needs a busybox with dd/od in /var/tmp (the stock one has neither).
P=$(pgrep cspd | head -1); echo "cspd pid $P"
/var/tmp/busybox dd if=/proc/$P/mem bs=4 skip=527580 count=6 2>/dev/null | /var/tmp/busybox od -An -tx4 -v
