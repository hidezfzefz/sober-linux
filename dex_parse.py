import struct,sys
p=sys.argv[1]; d=open(p,'rb').read()
o=0
magic=d[:8]; checksum,=struct.unpack_from('<I',d,8); sig=d[12:32]
(fs,hs,endian,ls,lo,mo)=struct.unpack_from('<IIIIII',d,32)
vals=struct.unpack_from('<IIIIIIIIIIIIII',d,56)
ss,so,ts,to,ps,po,fds,fdo,mids,mid_o,cds,cdo,ds,do=vals
print(f"DEX magic={magic!r} file_size={fs} strings={ss} types={ts} protos={ps} methods={mids} classes={cds}")
def uleb(buf,i):
    r=0;s=0
    while True:
        b=buf[i]; i+=1; r |= (b&0x7f)<<s
        if not b&0x80: return r,i
        s+=7
def sdata(off):
    n,i=uleb(d,off)
    raw=d[i:i+n]
    return raw.decode('utf-8','replace'),i+n
strings=[sdata(struct.unpack_from('<I',d,so+4*i)[0])[0] for i in range(ss)]
types=[strings[struct.unpack_from('<I',d,to+4*i)[0]] for i in range(ts)]
target='nativePostClientSettingsLoadedInitialization3'
for i in range(mids):
    c,pr,n=struct.unpack_from('<HHI',d,mid_o+8*i)
    if strings[n]==target:
        desc=types[c]
        shorty,ret,params_off=struct.unpack_from('<III',d,po+12*pr)
        plist=[]
        if params_off:
            cnt=struct.unpack_from('<I',d,params_off)[0]
            for k in range(cnt):
                plist.append(types[struct.unpack_from('<H',d,params_off+4+2*k)[0]])
        print(f"METHOD idx={i} class={desc} name={strings[n]} proto='({''.join(plist)}){types[ret]}'")
        tidx=next(j for j,t in enumerate(types) if t==desc)
        for j in range(cds):
            cdi,af,sup,ifoff,srcf,ann,cdoff,sv=struct.unpack_from('<IIIIIIII',d,cdo+32*j)
            if cdi==tidx:
                print(f"CLASS_DEF idx={j} access=0x{af:x} super={types[sup] if sup<ts else '?'} interfaces_off={ifoff} class_data_off={cdoff}")
                if ifoff:
                    nl=struct.unpack_from('<I',d,ifoff)[0]
                    print('INTERFACES:',[types[struct.unpack_from('<H',d,ifoff+4+2*k)[0]] for k in range(nl)])
                if cdoff:
                    i2=cdoff; sf,i2=uleb(d,i2); mf,i2=uleb(d,i2); gf,i2=uleb(d,i2)
                    print(f"CLASS_DATA static_fields={sf} instance_fields={mf} direct_methods={gf}")
                    for label,count in [('static',sf),('instance',mf)]:
                        fi=i2; idx=0
                        for k in range(count):
                            diff,fi=uleb(d,fi); idx+=diff
                            acc,fi=uleb(d,fi)
                            fn,ft,fo=struct.unpack_from('<HHI',d,fdo+8*idx)
                            print(f"  FIELD[{label}] {strings[fn]} : {types[ft]} access=0x{acc:x}")
