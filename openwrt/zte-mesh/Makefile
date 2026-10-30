# SPDX-License-Identifier: GPL-2.0-only
#
# The ZTE multiapd Controller runtime for the SR1010. The runtime tarball with
# the vendor binaries and an ns-run for /usr/lib/zte-mesh/root is committed next
# to this Makefile; regenerate it with `sh tools/build/runtime.sh install`.
# ZTE_MESH_RUNTIME can point at another one.
include $(TOPDIR)/rules.mk

PKG_NAME:=zte-mesh
PKG_VERSION:=0.1.0
PKG_RELEASE:=5
PKG_LICENSE:=GPL-2.0-only
# The tarball carries proprietary ARM32 vendor code; keep it out of shared builds.
PKG_FLAGS:=nonshared

include $(INCLUDE_DIR)/package.mk

define Package/zte-mesh
  SECTION:=net
  CATEGORY:=Network
  TITLE:=ZTE mesh controller runtime (sandboxed vendor multiapd)
  # Two targets are known to carry what the runtime needs (aarch64_cortex-a53 +
  # SysV IPC + AF_PACKET): the ZTE ZX279133 target this package was written for,
  # and generic IPQ807x boards. Everything else is still installable by hand with
  # --force-depends, but the dependency exists so a wrong board fails at install
  # time instead of at exec time (ENOEXEC needs CONFIG_COMPAT in the kernel).
  DEPENDS:=@(TARGET_zte||TARGET_qualcommax) +ip-full +kmod-veth +kmod-sched-core
  PKGARCH:=aarch64_cortex-a53
endef

define Package/zte-mesh/description
  Runs the ZTE multiapd Controller in a private IPC/network/mount namespace
  and attaches its wire to the LAN bridge. Requires AArch32 compat (CONFIG_COMPAT)
  and SysV IPC in the kernel. kmod-veth is a hard runtime requirement: the
  private wire is a veth pair, and without it the launcher stops with errno=2.
endef

define Package/zte-mesh/conffiles
/etc/config/zte_mesh
endef

ZTE_MESH_RUNTIME ?= $(CURDIR)/zte-mesh-runtime.tar.gz

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	tar -C $(PKG_BUILD_DIR) -xzf $(ZTE_MESH_RUNTIME)
endef

define Build/Compile
endef

define Package/zte-mesh/install
	$(INSTALL_DIR) $(1)/usr/lib/zte-mesh $(1)/usr/libexec/zte-mesh
	$(CP) $(PKG_BUILD_DIR)/root $(1)/usr/lib/zte-mesh/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ns-run $(1)/usr/libexec/zte-mesh/ns-run
	$(INSTALL_BIN) ./files/attach $(1)/usr/libexec/zte-mesh/attach
	$(INSTALL_BIN) ./files/wlan-sync $(1)/usr/libexec/zte-mesh/wlan-sync
	$(INSTALL_DIR) $(1)/etc/init.d $(1)/etc/config $(1)/usr/libexec/rpcd
	$(INSTALL_BIN) ./files/zte-mesh.init $(1)/etc/init.d/zte-mesh
	$(INSTALL_CONF) ./files/zte-mesh.config $(1)/etc/config/zte_mesh
	$(INSTALL_BIN) ./files/luci.zte-mesh $(1)/usr/libexec/rpcd/luci.zte-mesh
endef

$(eval $(call BuildPackage,zte-mesh))
