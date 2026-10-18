#!/bin/bash
# vt.sh 'shell text' [timeout]: run text on the vendor SR1010 over telnet (via the mac), print its output.
# The text is base64-sent in short chunks (the vendor line buffer truncates long lines).
# Needs ZTE_TN_PASS in the environment (never stored in a file).
B=$(printf '%s' "$1" | base64 -w0)
ARGS="': > /tmp/.vtb'"
for ((i=0; i<${#B}; i+=500)); do ARGS="$ARGS 'echo ${B:i:500} >> /tmp/.vtb'"; done
ARGS="$ARGS 'base64 -d /tmp/.vtb > /tmp/.vt.sh; rm -f /tmp/.vtb; echo @@VT-BEGIN@@; sh /tmp/.vt.sh 2>&1'"
ssh -o BatchMode=yes 192.168.167.93 "ZTE_TN_PASS='$ZTE_TN_PASS' python3 /Volumes/code/zx279133/port/mesh-shim/tools/child/ztetn.py --host 192.168.5.1 --port 23 --timeout ${2:-30} $ARGS" | tr -d '\r' | awk 'f{print} /^@@VT-BEGIN@@$/{f=1}'
