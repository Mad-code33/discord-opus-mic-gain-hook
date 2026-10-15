# Офлайн-проверка того, что hookdll ищет в discord_voice.node:
#  - таблица патчей (hookdll/patches.cpp): сигнатура уникальна, на месте — оригинал или патч;
#  - то же, что filters.cpp выводит в рантайме: ApplyConfig по строке лога, офсет
#    attenuation в ConnectionSettings, нужные экспорты.
#   python tools/sigcheck.py [discord_voice.node ...]
import bisect, os, re, struct, sys

LOCAL = os.path.expandvars(r'%LOCALAPPDATA%')
DEFAULT = [
    LOCAL + r'\Discord\app-1.0.9262\modules\discord_voice-1\discord_voice\discord_voice.node',
    LOCAL + r'\DiscordCanary\app-1.0.1234\modules\discord_voice-2\discord_voice\discord_voice.node',
]
# известные ответы FindAttn по размеру файла (stable 9261 / 9262, canary 1228 / 1234)
ATTN_EXPECT = {15567800: 0x618, 22312376: 0x620, 22352824: 0x620, 22360504: 0x648}

src = open(os.path.join(os.path.dirname(__file__), '..', 'hookdll', 'patches.cpp'), encoding='utf-8').read()
SITES = re.findall(r'\{\s*\w+,\s*"([0-9A-F? ]+)",\s*(-?0x[0-9A-Fa-f]+),\s*"([0-9A-F ]+)",\s*"([0-9A-F ]+)"\s*\}', src)
assert len(SITES) == 9, SITES

SET2 = ('?ApplySettings@Connection@voice@discord@@QEAAXAEBUConnectionSettings@3@'
        'V?$basic_string_view@DU?$char_traits@D@std@@@std@@@Z')
EXPORTS = [SET2, '?ApplySettings@Connection@voice@discord@@QEAAXAEBUConnectionSettings@3@@Z',
           '?SetVoiceSampleRateCap@Discord@@QEAAXI@Z', '?SetDuckingPreference@Discord@@QEAAX_N@Z']
core = open(os.path.join(os.path.dirname(__file__), '..', 'hookdll', 'hookcore.cpp'), encoding='utf-8').read()
HQ_SIGS = re.findall(r'(kSig\w+)\[\]\s*=\s*"(48 83 EC 28 [0-9A-F? ]+)"', core)   # opus-обёртки Stereo/HQ
assert len(HQ_SIGS) == 4, HQ_SIGS

CFG_PRO = bytes.fromhex('41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC')
ENC_PRO = bytes.fromhex('56 57 48 83 EC 28 BF FF FF FF FF 49 81 F8 80 16')   # kPrologue


def rx(sig):
    return re.compile(b''.join(b'.' if t == '??' else re.escape(bytes([int(t, 16)])) for t in sig.split()), re.S)


class Image:
    """PE, разложенный по RVA, как его видит загрузчик."""
    def __init__(self, d):
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        n, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
        o = pe + 24
        self.img = bytearray(struct.unpack_from('<I', d, o + 56)[0])
        self.img[:0x400] = d[:0x400]
        for i in range(n):
            _, vs, va, rs, ra = struct.unpack_from('<8sIIII', d, o + opt + i * 40)
            k = min(vs, rs)
            self.img[va:va + k] = d[ra:ra + k]
        self.img = bytes(self.img)
        ex_rva, pd_rva, pd_size = struct.unpack_from('<I', d, o + 112)[0], *struct.unpack_from('<II', d, o + 112 + 3 * 8)
        self.funcs = sorted(struct.unpack_from('<II', self.img, pd_rva + i * 12) for i in range(pd_size // 12))
        self.starts = [f[0] for f in self.funcs]
        nnames, fn, nm, od = struct.unpack_from('<IIII', self.img, ex_rva + 24)
        self.exports = {}
        for i in range(nnames):
            name = self.img[struct.unpack_from('<I', self.img, nm + 4 * i)[0]:].split(b'\0', 1)[0].decode()
            ordn = struct.unpack_from('<H', self.img, od + 2 * i)[0]
            self.exports[name] = struct.unpack_from('<I', self.img, fn + 4 * ordn)[0]

    def i32(self, a):
        return struct.unpack_from('<i', self.img, a)[0]

    # зеркало FindLeaTo (hookcore.cpp)
    def lea_to(self, s):
        at = self.img.find(s.encode() + b'\0')
        if at < 0:
            return None
        for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', self.img):
            p = m.start()
            if p + 7 + self.i32(p + 3) == at:
                return p
        return None

    def func_start(self, pc):
        i = bisect.bisect_right(self.starts, pc) - 1
        return self.funcs[i][0] if i >= 0 and pc < self.funcs[i][1] else None


# зеркало FindAttn (filters.cpp)
def find_attn(im):
    lea, apply = im.lea_to('attenuateWhileSpeakingOthers'), im.exports.get(SET2)
    if lea is None or apply is None:
        return 0
    m = re.compile(rb'\xc6\x84\x24(....)\x01', re.S).search(im.img, lea, lea + 0x80 + 7)
    if not m:
        return 0
    x = struct.unpack('<i', m.group(1))[0]
    # lea r8 — disp32 или disp8
    pat = re.compile(rb'\x48\x8d\xbc\x24(....)(?:\x4c\x8d\x84\x24....|\x4c\x8d\x44\x24.)\x48\x89\xfa\xe8', re.S)
    for c in pat.finditer(im.img, lea, lea + 0x4000 + 24):
        if c.end() + 4 + im.i32(c.end()) == apply:
            off = x - struct.unpack('<i', c.group(1))[0]
            return off if 0x400 < off < 0x1000 else 0
    return 0


bad = 0
for path in sys.argv[1:] or DEFAULT:
    d = open(path, 'rb').read()
    im = Image(d)
    print(path)
    for sig, off, orig, patch in SITES:
        orig, patch = bytes.fromhex(orig), bytes.fromhex(patch)
        assert len(orig) == len(patch)
        hits = [m.start() for m in rx(sig).finditer(im.img)]
        if len(hits) != 1:
            print(f'  n/a   {len(hits)} hits  {sig[:23]}...')
            bad += 1
            continue
        at = hits[0] + int(off, 16)
        cur = im.img[at:at + len(orig)]
        state = 'clean' if cur == orig else 'patch' if cur == patch else 'disk'
        print(f'  {state:5} rva {at:#08x}  {cur.hex(" ")}')

    # зеркало Resolve (hookcore.cpp): пролог Encode_wrapper уникален
    hits = [m.start() for m in re.finditer(re.escape(ENC_PRO), im.img)]
    ok = len(hits) == 1
    print(f'  {"ok" if ok else "FAIL":5} Encode_wrapper rva {hits[0] if ok else 0:#08x}')
    bad += not ok

    for name, sig in HQ_SIGS:   # зеркало InstallHqHooks: уникальный хит = начало функции
        hits = [m.start() for m in rx(sig).finditer(im.img)]
        ok = len(hits) == 1 and im.func_start(hits[0]) == hits[0]
        print(f'  {"ok" if ok else "FAIL":5} {name} rva {hits[0] if ok else 0:#08x}')
        bad += not ok

    lea = im.lea_to('AudioProcessing::ApplyConfig: ')
    cfg = im.func_start(lea) if lea is not None else None
    ok = cfg is not None and im.img[cfg:cfg + len(CFG_PRO)] == CFG_PRO
    print(f'  {"ok" if ok else "FAIL":5} ApplyConfig rva {cfg or 0:#08x}')
    bad += not ok

    attn = find_attn(im)
    want = ATTN_EXPECT.get(len(d))
    ok = attn != 0 and (want is None or attn == want)
    print(f'  {"ok" if ok else "FAIL":5} attenuation engaged +{attn:#x}' + (f' (want {want:#x})' if want else ''))
    bad += not ok

    for e in EXPORTS:
        ok = e in im.exports
        print(f'  {"ok" if ok else "FAIL":5} export {e[:48]}')
        bad += not ok
sys.exit(1 if bad else 0)
