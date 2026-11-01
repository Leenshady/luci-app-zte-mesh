#!/usr/bin/env python3
"""实验室三台机的统一 SSH 通道（密码登录）。

`tools/lab/rsh.py` 走 telnet，只服务中兴原厂子路由（Z506 只开 telnet + 明文
密码）。本文件是另一回事：2026-11-01 起实验台换成 immortalwrt 虚拟机 +
Ubuntu 虚拟机 + 小米 AX3600（刷 immortalwrt）三台，全部是 OpenSSH + 密码。
本机（Windows）没有 sshpass、没有 rsync，`ssh` 又不能喂密码，所以用 paramiko
当一个可脚本化的 scp/ssh。

机器表故意的：地址、账号、密码都写在 HOSTS 里，命令行只写别名。这样脚本和
CI 里不会散落凭据，改台账只改一处。

用法：
    python tools/lab/rssh.py owrt uname -a
    python tools/lab/rssh.py owrt --sh 'uci show wireless | head'
    python tools/lab/rssh.py ax3600 put zte-mesh.apk /tmp/
    python tools/lab/rssh.py ubuntu get /var/log/x.log ./x.log
    python tools/lab/rssh.py --all uname -a

退出码：远端命令的退出码原样返回；连接失败返回 255；用法错误返回 2。
"""

from __future__ import annotations

import argparse
import os
import posixpath
import sys
import time

try:
    import paramiko
except ImportError:  # 只有 3.13 那个解释器没有，miniconda 的有
    sys.stderr.write(
        "需要 paramiko。本机装在 miniconda："
        r"C:\Users\ZZB001\miniconda3\python.exe tools\lab\rssh.py ..." + "\n"
    )
    raise SystemExit(2)

# 别名 -> (地址, 用户, 密码, 说明)
HOSTS = {
    "ax3600": ("192.168.1.1", "root", "root", "小米 AX3600 / immortalwrt / ipq807x aarch64"),
    "owrt": ("192.168.1.20", "root", "root", "VM 里的 immortalwrt（实验台主控机）"),
    "ubuntu": ("192.168.1.191", "zzb", "123", "VM 里的 Ubuntu（编译机）"),
}

CONNECT_TIMEOUT = 15
CMD_TIMEOUT = 300


def connect(alias: str) -> paramiko.SSHClient:
    host, user, pwd, _ = HOSTS[alias]
    cli = paramiko.SSHClient()
    # 实验台机器随时重刷，host key 每次都会变；这里信任并记录，不做 pin。
    cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    cli.connect(
        host,
        port=22,
        username=user,
        password=pwd,
        timeout=CONNECT_TIMEOUT,
        banner_timeout=CONNECT_TIMEOUT,
        auth_timeout=CONNECT_TIMEOUT,
        look_for_keys=False,
        allow_agent=False,
    )
    return cli


def run(alias: str, command: str, quiet: bool = False, pty: bool = False) -> int:
    cli = connect(alias)
    try:
        # 默认**不开** pty：两台 OpenWrt 跑的是 dropbear，带 pty 的 exec 会把
        # 命令当成登录 shell 的输入，`echo HI` 都返回 127。需要 tty 的少数命令
        # （opkg 交互、top）用 --pty 显式要。
        stdin, stdout, stderr = cli.exec_command(command, timeout=CMD_TIMEOUT, get_pty=pty)
        stdin.close()
        out = stdout.read().decode("utf-8", "replace")
        err = stderr.read().decode("utf-8", "replace")
        code = stdout.channel.recv_exit_status()
        if not quiet:
            sys.stdout.write(out)
            if err.strip():
                sys.stdout.write(err)
            sys.stdout.flush()
        return code
    finally:
        cli.close()


def _sftp(alias: str):
    """开 sftp；拿不到就返回 None，交给 exec 通道。

    AX3600 的固件是 dropbear + 极简 image，**没有 sftp-server**（openssh-sftp-server
    未装），`open_sftp()` 会在协商阶段抛 "EOF during negotiation"。这不是连接问题，
    所以不能当失败退出 —— 退回 `cat` 管道即可，对二进制一样安全（SSH 通道是 8-bit
    clean 的，不做换行转换）。
    """
    cli = connect(alias)
    try:
        return cli, cli.open_sftp()
    except Exception:
        return cli, None


def put(alias: str, local: str, remote: str) -> int:
    if not os.path.exists(local):
        sys.stderr.write(f"本地不存在：{local}\n")
        return 2
    cli, sftp = _sftp(alias)
    try:
        if remote.endswith("/"):
            # SFTP 的 put 不会替你拼文件名，目录要自己接。
            remote = posixpath.join(remote, os.path.basename(local))
        if sftp is not None:
            sftp.put(local, remote)
            size = sftp.stat(remote).st_size
        else:
            data = open(local, "rb").read()
            stdin, stdout, stderr = cli.exec_command(f"cat > '{remote}'", timeout=CMD_TIMEOUT)
            stdin.write(data)
            stdin.channel.shutdown_write()
            err = stderr.read().decode("utf-8", "replace")
            if stdout.channel.recv_exit_status() != 0:
                sys.stderr.write(f"写入失败：{err}\n")
                return 1
            _, out, _ = cli.exec_command(f"wc -c < '{remote}'", timeout=30)
            size = int(out.read().decode().strip() or -1)
        local_size = os.path.getsize(local)
        print(f"{alias}:{remote}  {size} B" + ("" if size == local_size else f"  != {local_size} B"))
        return 0 if size == local_size else 1
    finally:
        cli.close()


def get(alias: str, remote: str, local: str) -> int:
    if local.endswith(("/", os.sep)):
        os.makedirs(local, exist_ok=True)
        local = os.path.join(local, posixpath.basename(remote))
    cli, sftp = _sftp(alias)
    try:
        if sftp is not None:
            sftp.get(remote, local)
        else:
            _, out, err = cli.exec_command(f"cat '{remote}'", timeout=CMD_TIMEOUT)
            data = out.read()
            if out.channel.recv_exit_status() != 0:
                sys.stderr.write(err.read().decode("utf-8", "replace"))
                return 1
            open(local, "wb").write(data)
        print(f"{local}  {os.path.getsize(local)} B")
        return 0
    finally:
        cli.close()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(
        add_help=True,
        description="实验台三台机的 SSH/scp（密码登录）",
        epilog="命令部分不归 argparse 管：第一个非选项 token 起，后面整串原样交给远端 shell。",
    )
    ap.add_argument("host", nargs="?", choices=sorted(HOSTS) + ["list"], help="别名，或 list")
    ap.add_argument("--all", action="store_true", help="对三台依次执行")
    ap.add_argument("--quiet", action="store_true", help="不打印远端输出，只看退出码")
    ap.add_argument("--bench", action="store_true", help="打印每台的 build 信息（架构/内核/发行版）")
    ap.add_argument("--pty", action="store_true", help="要一个 tty（少数交互命令）")

    # 为什么不用 argparse 的 REMAINDER：它会把 `--sh`/`--all` 这些控制参数也一起
    # 吞进 rest（实测 Namespace(rest=['--sh', 'echo HI'])），于是远端收到的是
    # `--sh echo HI`，ash 直接回 "ash: -c requires an argument"，退出码 2 —— 一
    # 个看起来像"连不上"的假故障。所以只把手上的已知选项吃掉，其余原样保留。
    flags = {"--all", "--quiet", "--bench", "--pty"}
    opts: list[str] = []
    command: list[str] = []
    it = iter(argv)
    for tok in it:
        if tok == "--":
            command.extend(it)
            break
        if tok in flags:
            opts.append(tok)
        elif tok.startswith("--") and not command:
            sys.stderr.write(f"未知选项：{tok}\n")
            return 2
        else:
            command.append(tok)
    a = ap.parse_args(opts + ([command[0]] if command and command[0] in list(HOSTS) + ["list"] else []))
    if command and command[0] in list(HOSTS) + ["list"]:
        command = command[1:]

    if a.host == "list" or (a.host is None and not a.all and not a.bench):
        for k, (h, u, _, d) in HOSTS.items():
            print(f"{k:8s} {u}@{h:15s} {d}")
        return 0

    targets = sorted(HOSTS) if (a.all or a.host is None) else [a.host]
    if a.bench:
        cmd = (
            'echo "## $(uname -n)"; uname -m; '
            'grep -m1 "model name\\|Hardware\\|system type" /proc/cpuinfo 2>/dev/null; '
            'uname -r; cat /etc/openwrt_release 2>/dev/null | head -3; '
            'cat /etc/os-release 2>/dev/null | head -2; '
            'nproc; free -m 2>/dev/null | head -2'
        )
        for t in targets:
            print(f"=== {t} ({HOSTS[t][0]})")
            run(t, cmd)
            print()
        return 0

    rest = command
    if not rest:
        sys.stderr.write("没有命令。例：rssh.py owrt 'uname -a'\n")
        return 2

    verb = rest[0]
    if verb in ("put", "get"):
        if len(rest) < 3:
            sys.stderr.write(f"用法：{verb} <src> <dst>\n")
            return 2
        rc = 0
        for t in targets:
            rc |= (put if verb == "put" else get)(t, rest[1], rest[2])
        return rc

    cmd = " ".join(rest)
    rc = 0
    for t in targets:
        if a.all:
            print(f"=== {t}")
        rc |= run(t, cmd, quiet=a.quiet, pty=a.pty)
    return rc


if __name__ == "__main__":
    t0 = time.time()
    sys.exit(main(sys.argv[1:]))
