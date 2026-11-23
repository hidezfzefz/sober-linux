import struct
d=open('/tmp/opencode/n2-dex/classes2.dex','rb').read()
vals=struct.unpack_from('<IIIIIIIIIIIIII',d,56)
ss,so,ts,to,ps,po,fds,fdo,mids,mid_o,cds,cdo,ds,do=vals
def uleb(b,i):
    r=0;s=0
    while True:
        x=b[i];i+=1;r|=(x&0x7f)<<s
        if not x&0x80:return r,i
        s+=7
def sd(off):
    n,i=uleb(d,off);return d[i:i+n].decode('utf-8','replace')
strings=[sd(struct.unpack_from('<I',d,so+4*i)[0]) for i in range(ss)]
types=[strings[struct.unpack_from('<I',d,to+4*i)[0]] for i in range(ts)]
def proto(p):
    sh,ret,po2=struct.unpack_from('<III',d,po+12*p)
    pl=[]
    if po2:
        c=struct.unpack_from('<I',d,po2)[0]
        pl=[types[struct.unpack_from('<H',d,po2+4+2*k)[0]] for k in range(c)]
    return '('+''.join(pl)+')'+types[ret]
target_off=0x335152
for j in range(cds):
    cdi,af,sup,ifo,sf,ann,cdoff,sv=struct.unpack_from('<IIIIIIII',d,cdo+32*j)
    if not cdoff: continue
    i=cdoff
    ds_,i=uleb(d,i); ms_,i=uleb(d,i); dm_,i=uleb(d,i); vm_,i=uleb(d,i)
    fidx=0
    for cnt in [ds_,ms_]:
        for k in range(cnt):
            diff,i=uleb(d,i); fidx+=diff
            acc,i=uleb(d,i)
    for kind,cnt in [('direct',dm_),('virtual',vm_)]:
        idx=0
        for k in range(cnt):
            diff,i=uleb(d,i); idx+=diff
            acc,i=uleb(d,i); codeoff,i=uleb(d,i)
            if not codeoff: continue
            ins_size=struct.unpack_from('<I',d,codeoff+12)[0]
            start=codeoff+16; end=start+2*ins_size
            if start<=target_off<end:
                mi=struct.unpack_from('<I',d,mid_o+8*idx)[0] if False else None
                cl,pr,nm=struct.unpack_from('<HHI',d,mid_o+8*idx)
                print(f"CALLER class={types[cdi]} method={strings[nm]} proto={proto(pr)} access=0x{acc:x} code_off={hex(codeoff)} insns={hex(start)}..{hex(end)} target={hex(target_off)}")
