/* InterfaceStack views the vendor AL-MAC path reads (VENDOR_BINARY_EVIDENCE 6.8).
 * Field order/type/save are the cspd constructor metadata; defaults are independent
 * public zeros, not source values. Only ViewName is a key. */
static const struct zte_db_domain startup_InterfaceStatus[] = {
    { .name = "ViewName", .type = 2, .primary_key = 1, .save = 1, .default_value = "" },
    { .name = "IF_ID", .type = 2, .primary_key = 0, .save = 0, .default_value = "" },
    { .name = "LowerLayer", .type = 7, .primary_key = 0, .save = 0, .default_value = "" },
    { .name = "Enable", .type = 15, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "WanLanPrority", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "LinkType", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "Alias", .type = 3, .primary_key = 0, .save = 0, .default_value = "" },
    { .name = "Status", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "LastChange", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "IFName", .type = 2, .primary_key = 0, .save = 0, .default_value = "" },
    { .name = "ttyDevName", .type = 2, .primary_key = 0, .save = 0, .default_value = "" },
    { .name = "UpMaxBitRate", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "DownMaxBitRate", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "UpCurrBitRate", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "DownCurrBitRate", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "PhyType", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "IFType", .type = 14, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "", .default_value = "0" }
};
static const struct zte_db_domain startup_EthLink[] = {
    { .name = "ViewName", .type = 2, .primary_key = 1, .save = 1, .default_value = "" },
    { .name = "Enable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "Status", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "Alias", .type = 3, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "Name", .type = 3, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "LastChange", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "LowerLayers", .type = 3, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "MACAddress", .type = 18, .primary_key = 0, .save = 1, .default_value = "00:00:00:00:00:00" },
    { .name = "PriorityTagging", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "WanLanPrority", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "VlanEnable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "WBMode", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "8021Priority", .type = 13, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "UserCfgMACAddress", .type = 18, .primary_key = 0, .save = 1, .default_value = "00:00:00:00:00:00" },
    { .name = "MACAddressOverride", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "DynamicLowyer", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "", .default_value = "0" }
};
static const struct zte_db_domain startup_Bridge[] = {
    { .name = "ViewName", .type = 2, .primary_key = 1, .save = 1, .default_value = "" },
    { .name = "Enable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "Status", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "Alias", .type = 3, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "Standard", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "StpEnable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "WanWanIsolate", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "WanLocalIsolate", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "AutoIpEnable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "ArpStolenEnable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "LanDhcpServerEnable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "MaxFDBNum", .type = 13, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "UpnpStolenEnable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "", .default_value = "0" }
};
static const struct zte_db_domain startup_Brport[] = {
    { .name = "ViewName", .type = 2, .primary_key = 1, .save = 1, .default_value = "" },
    { .name = "Enable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "Status", .type = 14, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "Alias", .type = 3, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "Name", .type = 3, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "HideState", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "LastChange", .type = 13, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "LowerLayers", .type = 6, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "ManagementPort", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "DefaultUserPriority", .type = 13, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "PriorityRegeneration", .type = 2, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "PortState", .type = 14, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "PVID", .type = 13, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "AcceptableFrameTypes", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "IngressFiltering", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "PriorityTagging", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "WanPortVlanEnable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "WanPortVlanMode", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "WanPortVlanID", .type = 13, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "WanPortPri", .type = 13, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "ServList", .type = 13, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "WanName", .type = 5, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "DynamicLowyer", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "IPMode", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "Origin", .type = 12, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "PortIndex", .type = 12, .primary_key = 0, .save = 0, .default_value = "0" },
    { .name = "", .default_value = "0" }
};
/* GetRouteIFInfo reads DHCP-provided DNS servers before it returns the interface MAC, and
 * QryDNSServersWithEnable makes that read a filtered dbAPIQryView. When the view is missing
 * the query fails, GetDNSServersByIPIF reports an error and GetRouteIFInfo stops before the
 * EthLink read - which is the "base get 1905 ALMAC fail" the vendor logs. The table itself
 * stays empty here (no DHCP lease in the sandbox); only a successful zero-row query is
 * needed. Field order/type come from dbCreateDNSClientServerTbl (cspd 0x1b3614). */
static const struct zte_db_domain startup_DNSClientServer[] = {
    { .name = "ViewName", .type = 2, .primary_key = 1, .save = 1, .default_value = "" },
    { .name = "Enable", .type = 15, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "Alias", .type = 3, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "DNSServerV4", .type = 16, .primary_key = 0, .save = 1, .default_value = "0.0.0.0" },
    { .name = "DNSServerV6", .type = 17, .primary_key = 0, .save = 1, .default_value = "::" },
    { .name = "Interface", .type = 5, .primary_key = 0, .save = 1, .default_value = "" },
    { .name = "Type", .type = 14, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "Priority", .type = 13, .primary_key = 0, .save = 1, .default_value = "0" },
    { .name = "", .default_value = "0" }
};
