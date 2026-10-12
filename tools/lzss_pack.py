"""Compressor for the game's LZSS format (linear mode). See lzss.py for the layout."""
def compress(data):
    out = bytearray(b"\0\0" + bytes([len(data) & 0xFF, len(data) >> 8, 1]))
    cmds = []; i = 0
    while i < len(data):
        best = (0, 0)
        for d in range(1, min(i, 4095) + 1):
            n = 0
            while n < 18 and i + n < len(data) and data[i + n - d] == data[i + n]: n += 1
            if n > best[0]: best = (n, d)
        if best[0] >= 3:
            n, d = best; cmds.append((0, bytes([d & 0xFF, (d >> 8) << 4 | (n - 3)]))); i += n
        else:
            cmds.append((1, bytes([data[i]]))); i += 1
    cmds.append((0, b"\0\x80"))   # end
    for k in range(0, len(cmds), 8):
        grp = cmds[k:k + 8]
        out.append(sum(f << j for j, (f, _) in enumerate(grp)))
        for _, b in grp: out += b
    return bytes(out)
if __name__ == "__main__":
    import sys; sys.path.insert(0, __file__.rsplit("/", 1)[0])
    from lzss import decompress
    import os
    for t in [bytes(range(50)) * 3, b"\x18" * 70, os.urandom(300)]:
        c = compress(t); assert decompress(c, 0)[0][:len(t)] == t, "roundtrip"
    print("ok")
