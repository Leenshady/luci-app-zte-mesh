#!/bin/sh
# build-launcher.sh：给装不上 zte-mesh-launcher 的目标机补那两个二进制。
#
# 为什么需要这个：zte-mesh-launcher 的 PKGARCH 是 aarch64_cortex-a53、DEPENDS 是
# @(TARGET_zte||TARGET_qualcommax)，x86_64 的 OpenWrt（虚拟机、软路由）两个条件都不满足，
# 一个都装不上。而 zte-mesh 本体是 PKGARCH:=all，能装。结果就是目标机上
# /usr/libexec/zte-mesh/ 里只有 attach 和 wlan-sync 两个脚本（它们属于 zte-mesh 包），
# 缺 ns-run（沙箱启动器）和 mesh-search（enrolment 触发），服务起不来。
#
# 这两个程序不依赖任何 OpenWrt 私有库，在编译机上用原生 gcc 编出来就能跑——不用交叉，
# 因为目标机和编译机同为 x86_64。CI 那条路要另开一个 x86 target 的 SDK，成本不成比例。
#
# 用法（Git Bash 下先 export MSYS_NO_PATHCONV=1，否则 /tmp 会被改写成 Windows 路径）：
#     tools/lab/build-launcher.sh
#     BUILD_HOST=ubuntu TARGET_HOST=owrt tools/lab/build-launcher.sh
#     SRC='~/zte-mesh' tools/lab/build-launcher.sh      # 编译机上的仓库路径
#
# 退出码：0 = 装好并自检通过；1 = 编译或传输失败；2 = 用法错误。

set -eu

BUILD_HOST=${BUILD_HOST:-ubuntu}
TARGET_HOST=${TARGET_HOST:-owrt}
PYTHON=${PYTHON:-python3}
# 引号别去掉：不加引号的话本机 shell 会把 ~ 展开成自己的家目录，再送过去就是错路径。
SRC=${SRC-'~/build'}
RSSH=${RSSH:-$(dirname "$0")/rssh.py}
# 远端和本机用的是两个不同的路径：本机 Python 是原生 Windows 解释器，它眼里的
# /tmp 是 C://tmp，跟 Git Bash 的 MSYS /tmp 不是同一个地方。所以本机侧一律用
# 项目内的相对路径，远端侧才用 /tmp。
REMOTE_WORK=${REMOTE_WORK:-/tmp/zte-lab-launcher}
LOCAL_WORK=${LOCAL_WORK:-out/lab-launcher}

say() { printf '%s\n' "$*"; }

if [ ! -f "$RSSH" ]; then
    say "找不到 $RSSH（用 RSSH=... 指定）" >&2
    exit 2
fi
if ! "$PYTHON" -c "import paramiko" 2>/dev/null; then
    say "$PYTHON 里没有 paramiko：pip install paramiko，或用 PYTHON=... 指定一个装了的解释器" >&2
    exit 2
fi

remote() { "$PYTHON" "$RSSH" "$1" "$2"; }

say "== 1/4 在 $BUILD_HOST 上编译（x86_64 原生）"
# 必须 -static。两条机器同为 x86_64，但编译机是 glibc、目标机是 musl：动态链接编出来的
# 程序在目标机上 execve 直接 "not found"（缺 /lib64/ld-linux-x86-64.so.2，不是缺文件）。
# 2026-11-01 真踩过——装上去服务起不来，日志里连一行自己的输出都没有。
remote "$BUILD_HOST" "
set -e
cd $SRC
mkdir -p $REMOTE_WORK
gcc -std=c11 -O2 -Wall -static -o $REMOTE_WORK/ns-run ns-run.c controller-net.c
gcc -std=c11 -O2 -Wall -static -Wno-unused-function -o $REMOTE_WORK/mesh-search tools/lab/enroller.c
ls -l $REMOTE_WORK/ns-run $REMOTE_WORK/mesh-search
"

say "== 2/4 取回本机（rssh 不做机对机）"
mkdir -p "$LOCAL_WORK"
for name in ns-run mesh-search; do
    "$PYTHON" "$RSSH" "$BUILD_HOST" get "$REMOTE_WORK/$name" "$LOCAL_WORK/$name"
done

say "== 3/4 装到 $TARGET_HOST（先停服务再备份）"
# 必须先停：ns-run 是正在跑的进程，覆盖它会得到 "Text file busy"。
remote "$TARGET_HOST" '/etc/init.d/zte-mesh stop >/dev/null 2>&1 || true
    sleep 2
    mkdir -p /tmp/zte-launcher-bak /usr/libexec/zte-mesh
    for f in ns-run mesh-search; do
        [ -f /usr/libexec/zte-mesh/$f ] && cp -a /usr/libexec/zte-mesh/$f /tmp/zte-launcher-bak/ || true
    done
    ls -l /tmp/zte-launcher-bak/ 2>/dev/null || true'
for name in ns-run mesh-search; do
    "$PYTHON" "$RSSH" "$TARGET_HOST" put "$LOCAL_WORK/$name" /usr/libexec/zte-mesh/$name
done
remote "$TARGET_HOST" 'chmod 755 /usr/libexec/zte-mesh/ns-run /usr/libexec/zte-mesh/mesh-search'

say "== 4/4 自检"
# 先验能不能 exec：glibc 动态链接的程序在 musl 上会得到"not found"，而这时候服务已经
# 停了，只看 status 只会看到一句 running 然后什么都没有。这一步放在重启之前。
# 注意不能只看退出码：ns-run 对 --help 返回 64（用法错误），跑不起来时是 127。
# 认 usage 文本才可靠。
if ! remote "$TARGET_HOST" '/usr/libexec/zte-mesh/ns-run --help 2>&1 | grep -q "^usage: ns-run"'; then
    say "ns-run 装上去跑不起来（多半是没加 -static，glibc 程序在 musl 上 execve 返回 ENOENT）。" >&2
    say "已装的文件留在 /usr/libexec/zte-mesh/，备份在 /tmp/zte-launcher-bak/。" >&2
    exit 1
fi
remote "$TARGET_HOST" '
echo "-- 归属："; for f in ns-run mesh-search; do printf "   %-12s " "$f"; \
    apk info -W /usr/libexec/zte-mesh/$f 2>&1 | head -1; done
echo "-- 服务："; /etc/init.d/zte-mesh restart >/dev/null 2>&1; sleep 10; \
    /etc/init.d/zte-mesh status 2>&1 | head -2
echo "-- 节点："; head -c 200 /run/zte-mesh/state.json 2>/dev/null; echo
'

say ""
say "完成。这两个文件不在任何包里，apk upgrade 不会删它们，但也不会更新它们——"
say "仓库里改了 ns-run.c 之后要重跑本脚本。"
