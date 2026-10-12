"""Quick linear 65816 disassembler (assumes 16-bit A/X unless told). usage: dis.py rom start_hex count [m] [x]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from op65816 import OPS
rom = open(sys.argv[1], 'rb').read()
a = int(sys.argv[2], 16); n = int(sys.argv[3]); m = int(sys.argv[4]) if len(sys.argv) > 4 else 0; x = int(sys.argv[5]) if len(sys.argv) > 5 else 0
def off(a): return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF)
L = {'imp':0,'acc':0,'imm8':1,'dp':1,'dpx':1,'dpy':1,'dpi':1,'dpxi':1,'dpiy':1,'dpil':1,'dpily':1,'sr':1,'sriy':1,'rel':1,
     'abs':2,'absx':2,'absy':2,'absi':2,'absxi':2,'absil':2,'rel16':2,'blk':2,'absl':3,'abslx':3}
for _ in range(n):
    o = off(a); op = rom[o]; mn, md = OPS[op]
    ln = (1 if m else 2) if md == 'immM' else (1 if x else 2) if md == 'immX' else L[md]
    arg = int.from_bytes(rom[o+1:o+1+ln], 'little')
    if mn == 'rep': m = 0 if arg & 0x20 else m; x = 0 if arg & 0x10 else x
    if mn == 'sep': m = 1 if arg & 0x20 else m; x = 1 if arg & 0x10 else x
    t = f'{arg:0{ln*2}X}' if ln else ''
    if md in ('rel',): t = f'{(a + 2 + (arg - 256 if arg > 127 else arg)) & 0xFFFF:04X}'
    print(f'{a:06X}  {rom[o:o+1+ln].hex():10s} {mn} {md} {t}')
    a = (a & 0xFF0000) | ((a + 1 + ln) & 0xFFFF)
