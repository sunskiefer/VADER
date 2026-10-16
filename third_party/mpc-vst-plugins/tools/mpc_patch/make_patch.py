#!/usr/bin/env python3
"""Build mpc-3.9.1.2.patch (our bytes only) from out/*.bin and check it against the user's own stock MPC binary.

    make_patch.py <stock MPC copied from the device> [-o patched-copy-for-checking]

Same patch format and install.sh as mpc-vst-machinedrum's release/mpc_patch, which this extends: the name check is a
table (matcher.S) instead of one hard-coded name. Nothing of Akai's is written to the patch file.
"""
import argparse
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STOCK_MD5 = "592eebc8e1ce0797dc8c98e7002143b8"
SITES = [0x18ff260, 0x18ffbd8, 0x18ffc84, 0x1900e04, 0x1900e98, 0x19016fc]   # movne r1,#8 -> #16
REGIONS = [  # (offset, bin, max size)
    (0x2494954, "helper", 16),
    (0x4a7b330, "cave2", 80),
    (0x2494bb4, "colours_jump", 12),
    (0x6872900, "colours", 0x80),
    (0x6872980, "matcher", 0x400),
]


def md5(b):
    return hashlib.md5(b).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stock")
    ap.add_argument("-o", help="also write the patched binary here (for checking; never commit it)")
    ap.add_argument("--patch", default=os.path.join(HERE, "mpc-3.9.1.2.patch"))
    a = ap.parse_args()
    stock = open(a.stock, "rb").read()
    if md5(stock) != STOCK_MD5:
        sys.exit("not the stock 3.9.1.2 MPC")
    lines, new = [], bytearray(stock)
    for off, name, cap in REGIONS:
        b = open(os.path.join(HERE, "out", name + ".bin"), "rb").read()
        if len(b) > cap:
            sys.exit("%s is %d bytes, room for %d" % (name, len(b), cap))
        if off in (0x4a7b330, 0x6872900, 0x6872980) and any(stock[off:off + max(len(b), 1)]):
            sys.exit("region at %#x is not empty in the stock binary" % off)
        lines.append("%x %s" % (off, b.hex()))
        new[off:off + len(b)] = b
    for s in SITES:
        if stock[s:s + 4] != bytes.fromhex("0810a013"):
            sys.exit("site %#x unexpected" % s)
        lines.append("%x 10" % s)
        new[s] = 0x10
    # decode the branch chain: helper -> cave2 -> matcher (each is ldr ip,[pc,#0]; add pc,pc,ip; .word delta)
    def target(base, word_off, add_off):
        d = struct.unpack("<I", bytes(new[base + word_off:base + word_off + 4]))[0]
        return (base + add_off + 8 + d) & 0xFFFFFFFF
    assert target(0x2494954, 12, 8) == 0x4a7b330, "helper does not reach cave2"
    assert target(0x4a7b330, 8, 4) == 0x6872980, "cave2 does not reach the matcher"
    assert target(0x2494bb4, 8, 4) == 0x6872900, "colour jump does not reach colours"
    pm = md5(bytes(new))
    with open(a.patch, "w", newline="\n") as f:
        f.write("# MPC OS 3.9.1.2 drum-layout patch v2 (name table): <hex offset> <new bytes>  (no Akai bytes; the device saves its own originals)\n")
        f.write("stock_md5 %s\n" % STOCK_MD5)
        f.write("patched_md5 %s\n" % pm)
        f.write("\n".join(lines) + "\n")
    if a.o:
        open(a.o, "wb").write(bytes(new))
    print("patched md5", pm, "-", len(lines), "regions")


main()
