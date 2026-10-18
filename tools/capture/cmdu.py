#!/usr/bin/env python3
"""cmdu.py PCAP [--tlv]: IEEE 1905.1 / Multi-AP CMDU sequence with direction (A->M child to master, M->A master to child)."""
import struct, sys, time
CM = {0:"TopoDiscovery",1:"TopoNotification",2:"TopoQuery",3:"TopoResponse",4:"VendorSpecific",5:"LinkMetricQuery",6:"LinkMetricResponse",
 7:"APAutoconfSearch",8:"APAutoconfResponse",9:"APAutoconfWSC",10:"APAutoconfRenew",11:"PushButtonEvent",12:"PushButtonJoin",13:"HigherLayerQuery",
 14:"HigherLayerResponse",15:"IfacePowerChgReq",16:"IfacePowerChgResp",17:"GenericPhyQuery",18:"GenericPhyResp",
 0x8000:"Ack",0x8001:"APCapQuery",0x8002:"APCapReport",0x8003:"PolicyConfigReq",0x8004:"ChanPrefQuery",0x8005:"ChanPrefReport",0x8006:"ChanSelReq",
 0x8007:"ChanSelResp",0x8008:"OperChanReport",0x8009:"ClientCapQuery",0x800a:"ClientCapReport",0x800b:"APMetricsQuery",0x800c:"APMetricsResp",
 0x800d:"AssocSTALinkQuery",0x800e:"AssocSTALinkResp",0x800f:"UnassocSTAQuery",0x8010:"UnassocSTAResp",0x8011:"BeaconMetricsQuery",
 0x8012:"BeaconMetricsResp",0x8013:"CombinedInfraMetrics",0x8014:"ClientSteeringReq",0x8015:"ClientSteeringBTMReport",0x8016:"ClientAssocControlReq",
 0x8017:"SteeringCompleted",0x8018:"HigherLayerData",0x8019:"BackhaulSteeringReq",0x801a:"BackhaulSteeringResp",0x801b:"ChanScanReq",0x801c:"ChanScanReport",
 0x8020:"CACReq",0x8021:"CACTerm",0x8022:"ClientDisassocStats",0x8023:"ServicePrioReq",0x8024:"ErrorResp",0x8025:"AssocStatusNotif",0x8026:"Tunneled",
 0x8027:"BHStaCapQuery",0x8028:"BHStaCapReport"}
TL = {0:"EOM",1:"ALMAC",2:"MAC",3:"DevInfo",4:"BridgeCap",6:"Non1905Nbr",7:"1905Nbr",8:"LinkMetricQ",9:"TxLinkMetric",10:"RxLinkMetric",0x0b:"Vendor",
 0x0c:"LinkMetricResult",0x0d:"SearchedRole",0x0e:"AutoconfBand",0x0f:"SupportedRole",0x10:"SupportedBand",0x11:"WSC",0x13:"PBJoin",0x15:"DevIdent",
 0x16:"ControlURL",0x17:"IPv4",0x18:"IPv6",0x1a:"Profile1905",0x80:"SupportedSvc",0x81:"SearchedSvc",0x82:"APRadioId",0x83:"APOperBSS",0x84:"AssocClients",
 0x85:"APRadioCaps",0x86:"APHTCaps",0x87:"APVHTCaps",0x88:"APHECaps",0x89:"SteeringPolicy",0x8a:"MetricRptPolicy",0x8b:"ChanPref",0x8c:"RadioOpRestr",
 0x8d:"TxPowerLimit",0x8e:"ChanSelResp",0x8f:"OperChanReport",0x90:"ClientInfo",0x91:"ClientCapReport",0x92:"ClientAssocEvent",0x93:"APMetricQuery",
 0x94:"APMetrics",0x95:"STAMACType",0x96:"AssocSTALink",0x97:"UnassocSTAQuery",0x98:"UnassocSTAResp",0x99:"BeaconQuery",0x9a:"BeaconResp",0x9b:"SteeringReq",
 0x9c:"SteeringBTMRpt",0x9d:"AssocCtlReq",0x9e:"BHSteeringReq",0x9f:"BHSteeringResp",0xa0:"HigherLayerData",0xa1:"APCapability",0xa2:"AssocSTATraffic",
 0xa3:"ErrorCode",0xb3:"MultiAPProfile",0xb4:"Profile2APCap",0xb5:"Default8021Q",0xb6:"TrafficSepPolicy"}
def tlvs(b):
    o=0
    while o+3<=len(b):
        t=b[o]; l=struct.unpack(">H",b[o+1:o+3])[0]; v=b[o+3:o+3+l]; o+=3+l
        yield t,v
        if t==0: break
PERIODIC={"APMetricsQuery","APMetricsResp","AssocSTALinkQuery","AssocSTALinkResp","LinkMetricQuery","LinkMetricResponse","TopoDiscovery","Ack"}
def main():
    d=open(sys.argv[1],'rb').read(); full="--tlv" in sys.argv
    since=0; quiet="--quiet" in sys.argv
    if "--since" in sys.argv:   # HH:MM:SS (vendor UTC clock) or epoch
        v=sys.argv[sys.argv.index("--since")+1]
        if ":" in v:
            h,m,sec=map(int,v.split(":")); since=None; sinceHMS=h*3600+m*60+sec
        else: since=float(v); sinceHMS=None
    else: sinceHMS=None
    magic,vmaj,vmin,tz,sig,snap,link=struct.unpack('<IHHiIII',d[:24]); o=24; n=0; t0=None
    while o+16<=len(d):
        ts,tu,cl,ol=struct.unpack('<IIII',d[o:o+16]); o+=16; fr=d[o:o+cl]; o+=cl; n+=1
        if len(fr)<22: continue
        t=ts+tu/1e6; t0=t0 or t
        if sinceHMS is not None and (ts%86400)<sinceHMS: continue
        if since and t<since: continue
        src=fr[6:12]; dst=fr[:6]
        dirn="A>M" if src[:5]==bytes.fromhex("d4b7090d77") else "M>A"
        if dst==bytes.fromhex("0180c2000013"): dirn+="*"
        ver,res,mt,mid,fid,flags=struct.unpack(">BBHHBB",fr[14:22])
        names=[]; vend=[]
        for tt,v in tlvs(fr[22:]):
            if tt==0: continue
            if tt==0x0b and v[:3]==bytes.fromhex("0019c6") and "--vendor" in sys.argv and len(v)>=6:
                subs=[]; o2=6
                while o2+3<=len(v):
                    st=v[o2]; sl=struct.unpack(">H",v[o2+1:o2+3])[0]; subs.append("%02x[%d]=%s"%(st,sl,v[o2+3:o2+3+sl].hex())); o2+=3+sl
                vend.append("hdr=%s %s"%(v[3:6].hex()," ".join(subs)))
            nm=TL.get(tt,"%#04x"%tt)
            if tt==0x0b: nm+="[%s:%s]"%(v[:3].hex(),v[3:5].hex())
            names.append(nm)
        if quiet and CM.get(mt,"") in PERIODIC: continue
        print("%3d %s %-4s %-22s mid=%04x frag=%d fl=%02x len=%-4d %s"%(n,time.strftime("%H:%M:%S",time.gmtime(t))+".%03d"%(tu//1000),dirn,CM.get(mt,"%#06x"%mt),mid,fid,flags,cl," ".join(names)))
        for vv in vend: print("        vendor:",vv)
main()
