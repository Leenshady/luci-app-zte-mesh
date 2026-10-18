#!/usr/bin/env python3
"""Minimal telnet client for the ZTE child: log in, run commands, print output.

usage: ztetn.py [--host H --port P] 'cmd1' 'cmd2' ...   (or commands on stdin, one per line)
Credentials come from the environment: ZTE_TN_USER (default root) and ZTE_TN_PASS (required, no default).
The default endpoint 192.168.174.5:2323 is a port forward (on the Mac mini) to the child's telnet.
"""
import argparse
import os
import socket
import sys
import time
import uuid

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240


class Tn:
    def __init__(self, host, port, timeout=15):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.settimeout(timeout)
        self.buf = b""

    def _pump(self, wait):
        self.s.settimeout(wait)
        try:
            data = self.s.recv(65536)
        except socket.timeout:
            return False
        if not data:
            raise EOFError("connection closed")
        out = bytearray()
        i = 0
        while i < len(data):
            b = data[i]
            if b == IAC and i + 1 < len(data):
                c = data[i + 1]
                if c in (DO, DONT, WILL, WONT) and i + 2 < len(data):
                    opt = data[i + 2]
                    if c == DO:
                        self.s.sendall(bytes([IAC, WONT, opt]))
                    elif c == WILL:
                        self.s.sendall(bytes([IAC, DONT, opt]))
                    i += 3
                    continue
                if c == SB:
                    j = data.find(bytes([IAC, SE]), i)
                    i = (j + 2) if j >= 0 else len(data)
                    continue
                if c == IAC:
                    out.append(IAC)
                i += 2
                continue
            out.append(b)
            i += 1
        self.buf += bytes(out)
        return True

    def expect(self, needle, timeout=15):
        end = time.time() + timeout
        needle = needle.encode() if isinstance(needle, str) else needle
        while time.time() < end:
            i = self.buf.find(needle)
            if i >= 0:
                got, self.buf = self.buf[:i + len(needle)], self.buf[i + len(needle):]
                return got
            self._pump(min(1.0, max(0.05, end - time.time())))
        raise TimeoutError("timeout waiting for %r; have %r" % (needle, self.buf[-200:]))

    def send(self, text):
        self.s.sendall(text.encode() + b"\r\n")

    def login(self, user, password):
        self.expect("Login:")
        self.send(user)
        self.expect("Password:")
        self.send(password)
        time.sleep(1.0)
        end = time.time() + 5
        while time.time() < end and self._pump(0.5):
            pass

    def run(self, cmd, timeout=30):
        tag = uuid.uuid4().hex[:8]
        end_mark = "__END_%s" % tag
        self.buf = b""
        self.send("%s; echo; echo __EN''D_%s" % (cmd, tag))
        # the echoed command line contains the quoted form, the real output the plain one
        data = self.expect("\n" + end_mark, timeout)
        text = data.decode("utf-8", "replace")
        lines = text.replace("\r", "").split("\n")
        return "\n".join(lines[1:-2]) if len(lines) > 3 else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.174.5")
    ap.add_argument("--port", type=int, default=2323)
    ap.add_argument("--timeout", type=int, default=30)
    ap.add_argument("commands", nargs="*")
    args = ap.parse_args()
    cmds = args.commands or [l.rstrip("\n") for l in sys.stdin if l.strip()]
    tn = Tn(args.host, args.port)
    if "ZTE_TN_PASS" not in os.environ:
        sys.exit("set ZTE_TN_PASS (and optionally ZTE_TN_USER) in the environment")
    tn.login(os.environ.get("ZTE_TN_USER", "root"), os.environ["ZTE_TN_PASS"])
    for cmd in cmds:
        print("$ " + cmd)
        print(tn.run(cmd, args.timeout))
    try:
        tn.send("exit")
    except OSError:
        pass


if __name__ == "__main__":
    main()
