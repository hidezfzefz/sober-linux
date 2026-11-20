#!/usr/bin/env python3
"""Decode Android APS2 packed relocations (AOSP linker_reloc_iterators.h)."""
import struct, sys

RELOCATION_GROUPED_BY_INFO_FLAG = 1
RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG = 2
RELOCATION_GROUPED_BY_ADDEND_FLAG = 4
RELOCATION_GROUP_HAS_ADDEND_FLAG = 8

class Decoder:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos
    def pop(self):
        value = 0
        shift = 0
        while True:
            if self.p >= len(self.d):
                raise EOFError("sleb128Decoder ran out of bounds")
            b = self.d[self.p]
            self.p += 1
            value |= (b & 0x7f) << shift
            shift += 7
            if not (b & 0x80):
                break
        if shift < 64 and (b & 0x40):
            value |= -(1 << shift)
        return value

def decode(data, pos=0):
    dec = Decoder(data, pos)
    num_relocs = dec.pop()
    r_offset = dec.pop()
    relocs = []
    r_addend = 0  # r_addend is a running accumulator across all relocations
    while len(relocs) < num_relocs:
        group_size = dec.pop()
        flags = dec.pop()
        delta = 0
        if flags & RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG:
            delta = dec.pop()
        r_info = 0
        if flags & RELOCATION_GROUPED_BY_INFO_FLAG:
            r_info = dec.pop()
        addend_mode = flags & (RELOCATION_GROUP_HAS_ADDEND_FLAG | RELOCATION_GROUPED_BY_ADDEND_FLAG)
        if addend_mode == (RELOCATION_GROUP_HAS_ADDEND_FLAG | RELOCATION_GROUPED_BY_ADDEND_FLAG):
            r_addend += dec.pop()
        for _ in range(group_size):
            if flags & RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG:
                r_offset += delta
            else:
                r_offset += dec.pop()
            if not (flags & RELOCATION_GROUPED_BY_INFO_FLAG):
                r_info = dec.pop()
            if addend_mode == RELOCATION_GROUP_HAS_ADDEND_FLAG:
                r_addend += dec.pop()
            relocs.append((r_offset & 0xffffffffffffffff, r_info, r_addend & 0xffffffffffffffff))
    return relocs, dec.p, num_relocs

if __name__ == "__main__":
    P = "/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so"
    f = open(P, "rb")
    f.seek(0x15e00)
    data = f.read(0x1eb1f2)
    assert data[:4] == b"APS2", data[:4]
    relocs, endpos, count = decode(data, 4)
    print("magic=APS2 declared_count=%d decoded=%d consumed=%d/%d (0x%x)" %
          (count, len(relocs), endpos, len(data), endpos))
    types = {}
    for off, info, add in relocs:
        t = info & 0xffffffff
        types[t] = types.get(t, 0) + 1
    print("r_info (low32) histogram:", {hex(k): v for k, v in sorted(types.items())})
    ia_lo, ia_hi = 0x7220e58, 0x7220e58 + 0x7110
    ia = [r for r in relocs if ia_lo <= r[0] < ia_hi]
    ia.sort()
    print("init_array relocations:", len(ia))
    for i, (off, info, add) in enumerate(ia[:12]):
        print("  [%d] off=%#x info=%#x addend=%#x" % (i, off, info, add))
    print("  ...")
    for i in range(len(ia)-4, len(ia)):
        off, info, add = ia[i]
        print("  [%d] off=%#x info=%#x addend=%#x" % (i, off, info, add))
