# SPDX-License-Identifier: GPL-2.0-only
#
# The two compiled halves of the ZTE mesh controller:
#
#   ns-run      builds the private IPC/network/mount namespace, execs the vendor
#               daemon inside it and wires the sandbox to the LAN bridge
#   mesh-search the controller half of the enrolment handshake, the stand-in for
#               the cspd search trigger the sandbox does not have (FEATURE_STATUS
#               B18); started by the "search" instance in zte-mesh.init
#
# Both are machine code, and an ELF runs on one architecture -- but the package is
# arch:all, because it ships one build per CPU (aarch64, armv7, x86_64) under
# /usr/libexec/zte-mesh/arch/ and a two-name shell entry that picks the directory
# matching `uname -m` and execs it (files/launcher-dispatch). One apk, any machine:
# that is the user-facing promise this packaging serves, and it replaced the old
# arrangement where the package was tagged aarch64_cortex-a53 and refused to
# install anywhere else -- taking it out of one `apk add` line also took the three
# arch:all packages down with it, because apk solves a transaction all or nothing.
#
# What the arch:all label gives up is the install-time rejection. `DEPENDS:=@(TARGET_zte||
# TARGET_qualcommax)` used to make `apk add` fail on a board that could not run the sandbox;
# arch:all installs by definition, so the question moved to run time: the dispatcher names the
# machine it is standing on, and zte-mesh.init asks the same thing before it starts an instance
# (procd would otherwise respawn a 127 every five seconds).
#
# The sources are deliberately not duplicated here. They travel inside the runtime tarball next
# to ../zte-mesh/Makefile -- that is what keeps them from drifting away from the repository
# (FEATURE_STATUS C5: a stale ns-run in a tarball once cost a whole day) -- and are unpacked
# from it at build time.
include $(TOPDIR)/rules.mk

PKG_NAME:=zte-mesh-launcher
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only

include $(INCLUDE_DIR)/package.mk

define Package/zte-mesh-launcher
  SECTION:=net
  CATEGORY:=Network
  TITLE:=ZTE mesh controller launcher and search helper (all architectures)
  # The board still has to be able to run what the sandbox holds, and that is not an
  # architecture question: the private wire is a veth pair, so without kmod-veth the
  # launcher stops with "error=private-service-network errno=2", and attaching it to
  # the bridge needs iproute2. Those requirements are the reason the package exists as
  # a dependency of nothing but itself; the CPU question is answered by the dispatcher.
  DEPENDS:=+zte-mesh +ip-full +kmod-veth +kmod-sched-core
  PKGARCH:=all
endef

define Package/zte-mesh-launcher/description
  Supplies /usr/libexec/zte-mesh/ns-run and mesh-search for the board, built from the
  launcher sources that travel inside the zte-mesh runtime tarball. One build per
  supported CPU (aarch64, armv7, x86_64) plus the shell entry that picks the one this
  machine runs; zte-mesh itself is arch:all and carries everything else.
endef

# The tarball lives in the sibling package directory, which is the one place it
# is committed. Point ZTE_MESH_RUNTIME elsewhere to build against another one.
ZTE_MESH_RUNTIME ?= $(CURDIR)/../zte-mesh/zte-mesh-runtime.tar.gz
# The multi-architecture build script sits at the top of the repository, next to the
# other build helpers, so the compiler search has one home. The tarball above is inside the
# feed and this is not: a checkout that exports only the package directories (a feed with no
# tools/ in it) will not find it, and Build/Compile says so instead of letting `sh` fail on an
# empty argument. Set ZTE_MESH_LAUNCHER_BUILD to the script in that case.
ZTE_MESH_LAUNCHER_BUILD ?= $(CURDIR)/../../tools/build/launcher-arches.sh

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	tar -C $(PKG_BUILD_DIR) -xzf $(ZTE_MESH_RUNTIME) ns-run.c controller-net.c enroller.c
endef

# One build per architecture the package carries. $(TARGET_CC) is the only toolchain an
# OpenWrt build tree guarantees, so it covers the architecture this tree is built for; the
# other CPUs come from host cross compilers when they are installed. Which CPUs got built
# is printed on purpose: an apk that carries one architecture is a package that installs
# everywhere and runs in one place, and the build log is where the difference is visible.
define Build/Compile
	[ -r $(ZTE_MESH_LAUNCHER_BUILD) ] || { \
		echo 'zte-mesh-launcher: no build script at $(ZTE_MESH_LAUNCHER_BUILD); set ZTE_MESH_LAUNCHER_BUILD to tools/build/launcher-arches.sh' >&2; \
		exit 1; \
	}
	ZTE_MESH_TARGET_CC="$(TARGET_CC)" \
	ZTE_MESH_TARGET_CFLAGS="$(TARGET_CFLAGS)" \
	sh $(ZTE_MESH_LAUNCHER_BUILD) $(PKG_BUILD_DIR) $(PKG_BUILD_DIR)/arch
endef

define Package/zte-mesh-launcher/install
	$(INSTALL_DIR) $(1)/usr/libexec/zte-mesh
	$(INSTALL_BIN) ./files/launcher-dispatch $(1)/usr/libexec/zte-mesh/ns-run
	$(CP) $(1)/usr/libexec/zte-mesh/ns-run $(1)/usr/libexec/zte-mesh/mesh-search
	$(CP) $(PKG_BUILD_DIR)/arch $(1)/usr/libexec/zte-mesh/
endef

$(eval $(call BuildPackage,zte-mesh-launcher))
