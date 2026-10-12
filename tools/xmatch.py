"""Cross-game address matcher: Super Star Wars and Super Empire Strikes Back share an engine.
For each SSW instruction that uses one of the given RAM addresses, take the surrounding instructions
as an opcode pattern (operands wildcarded), find it in the Empire ROM, and report the operand Empire
uses at the same position. usage: xmatch.py ADDR [ADDR...]   (hex, e.g. 044A 03FA)"""
import re, sys
from collections import defaultdict

def load_insns(srcdir, banks):
    out = []
    for b in banks:
        try:
            for line in open(f'{srcdir}/bank{b:02X}.s'):
                m = re.match(r'\s+(\w+)\s*(.*?)\s*;\s*([0-9A-F]{6})\s*$', line)
                if m and not line.lstrip().startswith('.'):
                    out.append((int(m.group(3), 16), m.group(1), m.group(2)))
        except FileNotFoundError:
            pass
    return out

def off(a): return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF)

# usage: xmatch.py <ssw.sfc> <sesb.sfc> <ssw disassembly dir> ADDR...   (the disassembly comes from tools/emit.py)
ssw = open(sys.argv[1], 'rb').read()
esb = open(sys.argv[2], 'rb').read()
ins = load_insns(sys.argv[3], range(0x80, 0xA0))
sys.argv = sys.argv[:1] + sys.argv[4:]
ins.sort()
# instruction lengths from consecutive addresses (within the same bank, small gaps only)
L = {}
for (a, _, _), (b, _, _) in zip(ins, ins[1:]):
    if b >> 16 == a >> 16 and 0 < b - a <= 4:
        L[a] = b - a
addrs = [int(x, 16) for x in sys.argv[1:] if not x.startswith('@')]
codes = [int(x[1:], 16) for x in sys.argv[1:] if x.startswith('@')]
W = int(__import__('os').environ.get('W', '6'))
results = defaultdict(lambda: defaultdict(int))
for i, (a, op, arg) in enumerate(ins):
    hit = [x for x in addrs if re.search(r'\$%04X\b' % x, arg)] + [('@', a) for c in codes if c == a]
    if not hit or a not in L:
        continue
    win = ins[max(0, i - W): i + W + 1]
    if any(w[0] not in L for w in win) or any(win[k + 1][0] - win[k][0] != L[win[k][0]] for k in range(len(win) - 1)):
        continue   # needs one contiguous run of code
    pat = b''; centre = 0
    for w in win:
        n = L[w[0]]; o = off(w[0])
        if w[0] == a: centre = len(pat)
        pat += re.escape(ssw[o:o + 1]) + b'.' * (n - 1)
    rx = re.compile(pat, re.S)
    ms = [m.start() for m in rx.finditer(esb)]
    if len(ms) != 1:
        continue
    eo = ms[0] + centre
    n = L[a]
    val = esb[eo + 1] | esb[eo + 2] << 8 if n >= 3 else esb[eo + 1]
    ebank = 0x80 + eo // 0x8000; eadr = 0x8000 + eo % 0x8000
    for x in hit:
        if isinstance(x, tuple):
            print(f'CODE SSW {a:06X} {op} {arg:24s} -> ESB {ebank:02X}:{eadr:04X} {esb[eo:eo + n].hex()}')
            continue
        results[x][val] += 1
        print(f'SSW {a:06X} {op} {arg:24s} -> ESB {ebank:02X}:{eadr:04X} operand ${val:04X}')
print()
for x in addrs:
    r = results[x]
    print(f'${x:04X}:', ', '.join(f'${v:04X} x{c}' for v, c in sorted(r.items(), key=lambda t: -t[1])) or 'no match')
