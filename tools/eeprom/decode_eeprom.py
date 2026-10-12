#!/usr/bin/env python3
"""Decode a9y18qlte camera EEPROM/OTP dumps (from eeprom_dump) with the live DT memory map.

usage: decode_eeprom.py <dt_dir> <eeprom_dir>
"""
import glob
import os
import struct
import sys
import zlib

DT, EE = sys.argv[1:3]
SUBDEV = {'dw9808': 12, 'dw9808_sub': 11, 'gt24p128c': 13, 'm24c64x': 14, 'lsi_s5k5e9yx_otp': 10}
ROLE = {'dw9808': 'rear (IMX576 main)', 'dw9808_sub': 'rear4 (S5K3M3 tele)', 'gt24p128c': 'front (IMX576)',
        'm24c64x': 'rear3 (S5K4HA ultra-wide)', 'lsi_s5k5e9yx_otp': 'rear2 (S5K5E9 depth, sensor OTP)'}


def s(b):
    return b.split(b'\0')[0].decode('latin-1').strip()


for d in sorted(glob.glob(os.path.join(DT, '*/qcom,eeprom@*'))):
    rd = lambda n: open(os.path.join(d, n), 'rb').read()
    name = s(rd('qcom,eeprom-name'))
    data = open(os.path.join(EE, f'eeprom_{SUBDEV[name]}.bin'), 'rb').read()
    mems = sorted([n for n in os.listdir(d) if n.startswith('qcom,mem') and n[8:].isdigit()], key=lambda n: int(n[8:]))
    regs = [struct.unpack('>6I', rd(n)) for n in mems]
    base = regs[0][1]   # OTP blocks start at a non-zero address; buffer is packed from it
    print(f"\n## {name:17} {ROLE[name]}  {len(data)} B")
    for k in range(0, len(regs) - 1, 2):
        size, addr = regs[k][0], regs[k][1] - base
        caddr = regs[k + 1][1] - base
        if name == 'lsi_s5k5e9yx_otp':   # OTP buffer is packed, not address-indexed
            addr, caddr = 0, size
        want = struct.unpack_from('<I', data, caddr)[0]
        got = zlib.crc32(data[addr:addr + size]) & 0xffffffff
        print(f"   section {k // 2}: 0x{addr:04x}..0x{addr + size - 1:04x} ({size:4} B) crc@0x{caddr:04x} {'OK ' if want == got else 'BAD'} ")
    if name == 'lsi_s5k5e9yx_otp':
        print(f"   fw/module: {s(data[0x2f:0x2f + 16])!r} cal-map: {s(data[0xe7:0xe7 + 16])!r}")
        continue
    hdr = 0x40 if name == 'gt24p128c' else 0x48
    print(f"   fw id @0x{hdr:02x}: {s(data[hdr:hdr + 12])!r}  cal map @0x{0x70 if name == 'gt24p128c' else 0xe0:02x}: "
          f"{s(data[0x70 if name == 'gt24p128c' else 0xe0:][:16])!r}")
    mid = 0x56 if name == 'gt24p128c' else 0xae
    print(f"   module id @0x{mid:02x}: {data[mid:mid + 16]!r}")
    if name in ('dw9808', 'dw9808_sub'):
        macro, pan = struct.unpack_from('<ii', data, 0x818)
        print(f"   AF cal: macro(D50 for tele)={macro} pan(inf)={pan}")
    if name == 'dw9808':
        for tag, dll, tx in (('rear2/depth', 0x08E6, 0x0946), ('rear4/tele', 0x0BE6, 0x0C46)):
            x, y, z = struct.unpack_from('<iii', data, tx)
            sx, sy = struct.unpack_from('<ii', data, tx + 0x60)
            ver = struct.unpack_from('<I', data, dll)[0]
            print(f"   dual-cal {tag:12} dll_ver={ver} cal@0x{dll + 4:04x}(512 B) tilt x/y/z={x}/{y}/{z} sx/sy={sx}/{sy}")
