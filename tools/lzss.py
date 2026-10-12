"""Super Star Wars LZSS decompressor (reimplementation of $81:8722, called via $81:88D0).

Block layout (at the pointer given to the loader):
  00 00            marker: compressed (anything else is the loader's other format)
  ss ss            output size (rounded up to even)
  mm               mode: 1 = linear, otherwise 2 = byte-interleaved (even bytes, then odd bytes)
  data             flag bytes, 8 commands each, LSB first:
                     1 -> literal byte
                     0 -> two bytes b0 b1:
                          b1 & $80, b1 & $40 -> run: (b1&$3F)+1 bytes counting up from b0
                          b1 & $80 only      -> end of stream
                          else distance = b0 | (b1>>4)<<8 (0 = end), length = (b1&$0F)+3
"""


class Bad(Exception):
    pass


def decompress(rom, o, max_in=0x10000):
    if rom[o] or rom[o + 1]:
        raise Bad("not compressed")
    size = ((rom[o + 2] | rom[o + 3] << 8) + 1) & ~1
    mode = rom[o + 4]
    if size == 0 or size > 0x10000:
        raise Bad("size")
    p = o + 5
    out = bytearray(size)
    pos = 0
    step = 1 if mode == 1 else 2

    def adv(n):
        nonlocal pos
        pos += n
        if step == 2 and pos >= size:
            pos = 1

    while True:
        if p - o > max_in or p >= len(rom):
            raise Bad("overrun")
        flags = rom[p]; p += 1
        for _ in range(8):
            if flags & 1:
                if pos >= size:
                    raise Bad("literal past end")
                out[pos] = rom[p]; p += 1
                adv(step)
            else:
                b0, b1 = rom[p], rom[p + 1]; p += 2
                if b1 & 0x80:
                    if not b1 & 0x40:
                        return bytes(out), p - o, mode
                    cnt = (b1 & 0x3F) + 1
                    v = b0
                    for i in range(cnt):
                        q = pos + i * step
                        if q >= size:
                            raise Bad("run past end")
                        out[q] = v & 0xFF; v += 1
                    adv(cnt * step)
                else:
                    dist = b0 | (b1 >> 4) << 8
                    if dist == 0:
                        return bytes(out), p - o, mode
                    ln = (b1 & 0x0F) + 3
                    if step == 1:
                        src = pos - dist
                        if src < 0 or pos + ln > size:
                            raise Bad("ref out of range")
                        for i in range(ln):
                            out[pos + i] = out[src + i]
                        pos += ln
                    else:
                        src = pos - dist * 2
                        if src < 0:
                            src += size - 1
                        for i in range(0, ln * 2, 2):
                            if pos + i >= size or src + i >= size or src + i < 0:
                                raise Bad("ref out of range")
                            out[pos + i] = out[src + i]
                        adv(ln * 2)
            flags >>= 1
