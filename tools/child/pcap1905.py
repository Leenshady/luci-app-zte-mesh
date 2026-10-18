#!/usr/bin/env python3
import struct, sys
NAMES={0:"topology discovery",1:"topology notification",2:"topology query",3:"topology response",4:"vendor specific",5:"link metric query",6:"link metric response",7:"AP-autoconfig search",8:"AP-autoconfig response",9:"AP-autoconfig WSC",10:"AP-autoconfig renew",11:"push button event notification",12:"push button join notification",13:"higher layer query",14:"higher layer response",15:"interface power change request",16:"interface power change response",17:"generic phy query",18:"generic phy response"}
def tlvs(b):
    o=0
    while o+3<=len(b):
        t=b[o]; l=struct.unpack(">H",b[o+1:o+3])[0]; v=b[o+3:o+3+l]; o+=3+l
        yield t,v
        if t==0: break
d=open(sys.argv[1],'rb').read()
magic,vmaj,vmin,tz,sig,snap,link=struct.unpack('<IHHiIII',d[:24]); o=24; n=0
while o+16<=len(d):
    ts,tu,cl,ol=struct.unpack('<IIII',d[o:o+16]); o+=16; fr=d[o:o+cl]; o+=cl; n+=1
    dst,src,et=fr[:6].hex(':'),fr[6:12].hex(':'),fr[12:14].hex()
    ver,res,mt,mid,fid,flags=struct.unpack(">BBHHBB",fr[14:22])
    print("#%d t=%d.%06d len=%d %s -> %s eth=%s  CMDU %d (%s) mid=%#x frag=%d flags=%#x"%(n,ts,tu,cl,src,dst,et,mt,NAMES.get(mt,"?"),mid,fid,flags))
    for t,v in tlvs(fr[22:]):
        extra=""
        if t==0x0b or t==11: extra=" vendor OUI=%s data=%s"%(v[:3].hex(':'),v[3:].hex())
        elif t==1: extra=" AL MAC "+v.hex(':')
        elif t==0xb3: extra=" Multi-AP Profile=%s"%v.hex()
        elif t==0x0f: extra=" searched role=%s"%v.hex()
        elif t==0x10: extra=" searched freq band=%s"%v.hex()
        elif t==0x11: extra=" service=%s"%v.hex()
        elif t==0x80: extra=" supported svc=%s"%v.hex()
        else: extra=" "+v[:48].hex()+("…" if len(v)>48 else "")
        print("    TLV %#04x len=%d%s"%(t,len(v),extra))
