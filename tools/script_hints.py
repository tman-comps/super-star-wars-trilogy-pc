"""Turn build/scripts.json into tracer hints: native entry points + script byte ranges."""
import json
S = json.load(open('build/scripts.json'))
out = []
for a in S['natives']:
    out.append(f"label {a} 0 0")


def snes(o):
    return 0x800000 | ((o // 0x8000) << 16) | 0x8000 | (o % 0x8000)


for k, e in S['ops'].items():
    out.append(f"data {snes(int(k)):06X} {e['len']:X}")
open('build/script_hints.txt', 'w').write("\n".join(out) + "\n")
print(len(S['natives']), 'native entries,', len(S['ops']), 'script ops')
