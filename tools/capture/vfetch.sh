#!/bin/bash
# vfetch.sh VENDORFILE LOCALFILE: copy a file off the vendor over telnet-started nc (vendor -> mac -> here)
SP=$(dirname "$0")
P=$((7100 + RANDOM % 800))
$SP/vt.sh "(nc -l -p $P < $1 > /dev/null 2>&1 &); sleep 1; echo up" 20 > /dev/null
sleep 1
ssh -o BatchMode=yes 192.168.167.93 "nc -w 8 192.168.5.1 $P" > "$2"
wc -c < "$2"
