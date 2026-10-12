"""Headless snes9x (libretro) harness with code/data logging.

cdl byte per 24-bit address:
  0x01 exec m=1  0x02 exec m=0  0x04 exec x=1  0x08 exec x=0  0x10 exec emu-mode
  0x40 read by CPU   0x80 read by DMA
"""
import ctypes as C
import os

HERE = os.path.dirname(os.path.abspath(__file__))
BTN = {"B": 0, "Y": 1, "SELECT": 2, "START": 3, "UP": 4, "DOWN": 5, "LEFT": 6,
       "RIGHT": 7, "A": 8, "X": 9, "L": 10, "R": 11}


class GameInfo(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p), ("size", C.c_size_t), ("meta", C.c_char_p)]


ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VID = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUD = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDB = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL = C.CFUNCTYPE(None)
INP = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)


class Emu:
    def __init__(self, rom, core=os.path.join(HERE, "snes9x_libretro.so")):
        self.L = C.CDLL(core)
        self.cdl = (C.c_ubyte * 0x1000000)()
        self.L.cdl_set(self.cdl)
        self.buttons = set()
        self.buttons2 = set()
        self.frame = None
        self.fw = self.fh = self.pitch = 0
        self._dir = C.c_char_p(b"/tmp")
        self._cbs = [ENV(self._env), VID(self._vid), AUD(lambda l, r: None),
                     AUDB(lambda d, n: n), POLL(lambda: None), INP(self._inp)]
        L = self.L
        L.retro_set_environment(self._cbs[0])
        L.retro_set_video_refresh(self._cbs[1])
        L.retro_set_audio_sample(self._cbs[2])
        L.retro_set_audio_sample_batch(self._cbs[3])
        L.retro_set_input_poll(self._cbs[4])
        L.retro_set_input_state(self._cbs[5])
        L.retro_init()
        self.romdata = open(rom, "rb").read()
        self._rombuf = C.create_string_buffer(self.romdata, len(self.romdata))
        gi = GameInfo(rom.encode(), C.cast(self._rombuf, C.c_void_p), len(self.romdata), None)
        assert L.retro_load_game(C.byref(gi)), "load failed"
        L.retro_get_memory_data.restype = C.c_void_p
        L.retro_get_memory_size.restype = C.c_size_t
        L.retro_serialize_size.restype = C.c_size_t
        L.retro_serialize.restype = C.c_bool
        L.retro_unserialize.restype = C.c_bool
        ptr = L.retro_get_memory_data(2)
        self.wram = (C.c_ubyte * L.retro_get_memory_size(2)).from_address(ptr)
        self.frames = 0
        self.log = (C.c_uint * (1 << 24))()
        self.L.cdl_set_log(self.log, 1 << 24)
        self.L.cdl_get_logn.restype = C.c_uint
        self.per_frame = []      # callables run before every frame (RAM pokes)

    def watch(self, i, pc, dp):
        self.L.cdl_set_watch(i, pc & 0x7FFFFF, dp)

    def take_log(self):
        n = self.L.cdl_get_logn()
        out = [(self.log[k], self.log[k + 1]) for k in range(0, n, 2)]
        self.L.cdl_reset_log()
        return out

    def _env(self, cmd, data):
        cmd &= 0xFFFF
        if cmd in (9, 31):  # system / save dir
            C.cast(data, C.POINTER(C.c_char_p))[0] = self._dir
            return True
        if cmd == 10:  # pixel format
            self.fmt = C.cast(data, C.POINTER(C.c_int))[0]
            return True
        if cmd == 3:
            return True
        return False

    def _vid(self, data, w, h, pitch):
        if data:
            self.fw, self.fh, self.pitch = w, h, pitch
            self.frame = C.string_at(data, pitch * h)

    def _inp(self, port, dev, idx, i):
        if port not in (0, 1) or dev != 1:
            return 0
        bs = self.buttons if port == 0 else self.buttons2
        if i == 256:  # bitmask query
            return sum(1 << BTN[b] for b in bs)
        for b in bs:
            if BTN[b] == i:
                return 1
        return 0

    def run(self, n=1, buttons=()):
        self.buttons = set(buttons)
        for _ in range(n):
            for f in self.per_frame:
                f(self)
            self.L.retro_run()
            self.frames += 1
        self.buttons = set()

    def press(self, b, hold=4, after=10):
        self.run(hold, [b] if isinstance(b, str) else b)
        self.run(after)

    def save(self):
        n = self.L.retro_serialize_size()
        buf = C.create_string_buffer(n)
        assert self.L.retro_serialize(buf, n)
        return buf.raw

    def load(self, st):
        buf = C.create_string_buffer(st, len(st))
        assert self.L.retro_unserialize(buf, len(st))

    def r8(self, a):
        return self.wram[a & 0x1FFFF]

    def r16(self, a):
        return self.wram[a & 0x1FFFF] | (self.wram[(a + 1) & 0x1FFFF] << 8)

    def w8(self, a, v):
        self.wram[a & 0x1FFFF] = v & 0xFF

    def w16(self, a, v):
        self.w8(a, v); self.w8(a + 1, v >> 8)

    def screenshot(self, path):
        from PIL import Image
        w, h, p = self.fw, self.fh, self.pitch
        img = Image.new("RGB", (w, h))
        px = img.load()
        fr = self.frame
        for y in range(h):
            row = fr[y * p:y * p + w * 2]
            for x in range(w):
                v = row[2 * x] | (row[2 * x + 1] << 8)
                px[x, y] = (((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)
        img.save(path)
