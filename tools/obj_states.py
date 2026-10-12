"""Enumerate every object type's state table -> VM0 script entry points.

$82:FDCD  word table: object type (x2) -> definition record (bank $82, $36 bytes)
def+$2F   24-bit pointer to state table: [state byte][script word] ... FF
script bank = bank of the state table pointer
"""
import json
d = open('rom/ssw.sfc', 'rb').read()


def off(a):
    return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF)


T = off(0x82FDCD)
types = {}
for i in range(1, 128):
    p = d[T + 2 * i] | d[T + 2 * i + 1] << 8
    if p < 0x8000:
        break
    r = off(0x820000 | p)
    st = d[r + 0x2F] | d[r + 0x30] << 8 | d[r + 0x31] << 16
    states = []
    if (st & 0xFFFF) >= 0x8000:
        o = off(st)
        for k in range(64):
            if d[o + 3 * k] == 0xFF:
                break
            states.append([d[o + 3 * k], (st & 0xFF0000) | d[o + 3 * k + 1] | d[o + 3 * k + 2] << 8])
    types[i * 2] = {"def": 0x820000 | p, "state_table": st, "states": states}
json.dump({f"{k:02X}": v for k, v in types.items()}, open('build/obj_types.json', 'w'), indent=0)
seeds = sorted({s for t in types.values() for _, s in t["states"]})
json.dump({"fetch": [[0, a, 0] for a in seeds], "pairs": []}, open('build/obj_seeds.scr.json', 'w'))
print(len(types), 'types,', sum(len(t["states"]) for t in types.values()), 'states,', len(seeds), 'unique scripts')
