# tools/

Helper scripts that used to live in `/tmp` or in a scratch directory. None of them contains credentials.

| Path | What it is |
|---|---|
| `build/macmini-build.sh` | Runs on the Mac mini build host (`192.168.174.5`, ssh port 22, repo at `/Volumes/code/zx279133`): builds `db-engine-probe` and `zte-meshctl` (ARM32, soft-float) in the offline Docker builder `sr1010-mesh-shim-builder`. |
| `build/macmini-build-debugcmd.sh` | Same builder, static `zte-debugcmd`. |
| `build/repack-svctest.py` | Replaces `libzte_controller_profile.so`, `db-engine-probe`, `zte-meshctl` inside `out/mesh-shim/zte-mesh-svctest.tar.gz` and regenerates the in-archive `SHA256SUMS`. Expects the three files in `/tmp/svctest-repack/`; writes `zte-mesh-svctest.tar.gz.new`. |
| `child/ztetn.py` | Minimal telnet client for the child (default endpoint `192.168.174.5:2323`, a port forward to the child). Credentials only from `ZTE_TN_USER` / `ZTE_TN_PASS`. Usage: `python3 ztetn.py --timeout 60 < script.sh`. |
| `child/child-state.sh` | Read-only snapshot: `MultiAPCfg`, `P1905ComCfg`, `map.slave dev`, process list. |
| `child/child-acl.sh` | ACL rules/policies in the DB and in the driver (`ACLShowAll`). |
| `child/child-election-flag.sh` | Reads `g_TriggerSelectFlag` from `cspd` memory (role-election trigger, see FINDINGS.md section 3). |
| `child/child-log-follow.sh` | Follows the child log for 160 s (`logctrl -r -g 1 -n`). |
| `child/pcap1905.py` | Tiny IEEE 1905 pcap dumper (CMDU type, TLV list) for the captures `dev-service-smoke.py --pcap-out` writes. |
| `drivers/example-*.py` | Session test drivers (channel/power, ACL, ACL ban of a real client) kept as examples; adapt paths first. |

Routes: SR1010 = `ssh -p 2022 root@192.168.174.5` (the harness default), build host = `ssh 192.168.174.5` (port 22).
