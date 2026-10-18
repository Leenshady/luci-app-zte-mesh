# SPDX-License-Identifier: GPL-2.0-only
#
# The ZTE multiapd Controller runtime for the SR1010. The vendor binaries are
# NOT part of this repository: build the runtime tarball with
#   sh port/mesh-shim/build.sh
#   python3 port/mesh-shim/openwrt/make-runtime.py --ns-run out/mesh-shim/ns-run
# (an ns-run built WITHOUT -DSERVICE_ROOT, i.e. for /usr/lib/zte-mesh/root) and
# point ZTE_MESH_RUNTIME at it (default: $(TOPDIR)/dl/zte-mesh-runtime.tar.gz).
include $(TOPDIR)/rules.mk

PKG_NAME:=zte-mesh
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
# The tarball carries proprietary ARM32 vendor code that is kept private.
PKG_FLAGS:=nonshared

include $(INCLUDE_DIR)/package.mk

define Package/zte-mesh
  SECTION:=net
  CATEGORY:=Network
  TITLE:=ZTE mesh controller runtime (sandboxed vendor multiapd)
  DEPENDS:=@TARGET_zte +ip-full +kmod-veth +kmod-sched-core
  PKGARCH:=aarch64_cortex-a53
endef

define Package/zte-mesh/description
  Runs the ZTE multiapd Controller in a private IPC/network/mount namespace
  and attaches its wire to the LAN bridge. Requires AArch32 compat (CONFIG_COMPAT)
  and SysV IPC in the kernel.
endef

define Package/zte-mesh/conffiles
/etc/config/zte_mesh
endef

ZTE_MESH_RUNTIME ?= $(TOPDIR)/dl/zte-mesh-runtime.tar.gz

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
	$(INSTALL_DIR) $(1)/etc/init.d $(1)/etc/config $(1)/usr/libexec/rpcd
	$(INSTALL_BIN) ./files/zte-mesh.init $(1)/etc/init.d/zte-mesh
	$(INSTALL_CONF) ./files/zte-mesh.config $(1)/etc/config/zte_mesh
	$(INSTALL_BIN) ./files/luci.zte-mesh $(1)/usr/libexec/rpcd/luci.zte-mesh
endef

$(eval $(call BuildPackage,zte-mesh))
