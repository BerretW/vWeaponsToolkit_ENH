"""Independent stdlib-only RSC7/YTD checks; run after ResourceWriterTests."""
import math
import struct
import sys
import unittest
import zlib
from pathlib import Path


def page_size(flags):
    counts = [
        (flags >> 27) & 1, (flags >> 26) & 1, (flags >> 25) & 1,
        (flags >> 24) & 1, (flags >> 17) & 127, (flags >> 11) & 63,
        (flags >> 7) & 15, (flags >> 5) & 3, (flags >> 4) & 1,
    ]
    return (512 << (flags & 15)) * sum(n << i for i, n in enumerate(counts))


def page_count(flags):
    return sum([
        (flags >> 27) & 1, (flags >> 26) & 1, (flags >> 25) & 1,
        (flags >> 24) & 1, (flags >> 17) & 127, (flags >> 11) & 63,
        (flags >> 7) & 15, (flags >> 5) & 3, (flags >> 4) & 1,
    ])


def joaat(name):
    value = 0
    for c in name.lower().encode("ascii"):
        value = (value + c) & 0xFFFFFFFF
        value = (value + (value << 10)) & 0xFFFFFFFF
        value ^= value >> 6
    value = (value + (value << 3)) & 0xFFFFFFFF
    value ^= value >> 11
    return (value + (value << 15)) & 0xFFFFFFFF


class Resource:
    def __init__(self, path):
        raw = path.read_bytes()
        magic, self.version, sf, gf = struct.unpack_from("<4I", raw)
        assert magic == 0x37435352
        assert self.version == ((sf >> 28) << 4) | (gf >> 28)
        self.system_size = page_size(sf)
        self.graphics_size = page_size(gf)
        inflater = zlib.decompressobj(-15)
        self.data = inflater.decompress(raw[16:]) + inflater.flush()
        assert inflater.eof and not inflater.unused_data
        assert len(self.data) == self.system_size + self.graphics_size
        table = self.unpack("Q", 0x50000008)
        sp, gp = self.unpack("BB", table + 8)
        assert (sp, gp) == (page_count(sf), page_count(gf))
        self.read(table, 16 + 8 * (sp + gp))

    def read(self, pointer, size):
        segment = pointer & ~0x0FFFFFFF
        offset = pointer & 0x0FFFFFFF
        if segment == 0x50000000:
            assert offset + size <= self.system_size
        else:
            assert segment == 0x60000000
            assert offset + size <= self.graphics_size
            offset += self.system_size
        return self.data[offset:offset + size]

    def unpack(self, fmt, pointer):
        values = struct.unpack("<" + fmt, self.read(pointer, struct.calcsize("<" + fmt)))
        return values[0] if len(values) == 1 else values

    def name(self, pointer):
        result = bytearray()
        for i in range(128):
            c = self.unpack("B", pointer + i)
            if not c:
                return result.decode("ascii")
            result.append(c)
        raise AssertionError("Unterminated texture name")

    def textures(self, strict=True):
        hashes, count, capacity = self.unpack("QHH", 0x50000020)
        pointers, tc, tcap = self.unpack("QHH", 0x50000030)
        assert count == capacity == tc == tcap
        result = {}
        previous = -1
        for i in range(count):
            hash_value = self.unpack("I", hashes + 4 * i)
            assert hash_value > previous
            previous = hash_value
            tex = self.unpack("Q", pointers + 8 * i)
            name = self.name(self.unpack("Q", tex + 0x28))
            assert hash_value == joaat(name)
            if self.version == 5:
                w, h, depth, dimension, fmt = self.unpack("HHHBB", tex + 0x18)
                assert depth == dimension == 1
                fmt = {71: "BC1", 77: "BC3"}[fmt]
                levels = self.unpack("B", tex + 0x22)
                data = self.unpack("Q", tex + 0x38)
                stride = self.unpack("I", tex + 0xC)
                assert stride == (8 if fmt == "BC1" else 16)
                srv = self.unpack("Q", tex + 0x30)
                self.read(srv, 32)
                if strict:
                    assert self.unpack("I", tex + 0x10) == 0x00260208
                    assert self.unpack("B", tex + 0x20) == 255
                    assert srv == tex + 0x58
                    assert self.unpack("Q", srv) == 0x1406B77D8
                    assert self.unpack("HHI", srv + 16) == (0x41, 65535, 0xFFFFFFFF)
                    assert self.unpack("I", tex + 0x40) == 20
            else:
                assert self.version == 13
                w, h, depth, stride, fmt = self.unpack("HHHHI", tex + 0x50)
                fmt = {0x31545844: "BC1", 0x35545844: "BC3"}[fmt]
                assert depth == 1
                assert stride == max(1, (w + 3) // 4) * (8 if fmt == "BC1" else 16) // 4
                levels = self.unpack("B", tex + 0x5D)
                data = self.unpack("Q", tex + 0x70)
            mips, blocks = [], 0
            for _ in range(levels):
                nblocks = max(1, (w + 3) // 4) * max(1, (h + 3) // 4)
                blocks += nblocks
                size = nblocks * (8 if fmt == "BC1" else 16)
                mips.append((w, h, fmt, self.read(data, size)))
                data += size
                w, h = max(1, w // 2), max(1, h // 2)
            if self.version == 5 and strict:
                assert self.unpack("I", tex + 8) == blocks
            result[name] = mips
        return result


def color565(c):
    r, g, b = c >> 11, (c >> 5) & 63, c & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def decode(mip):
    w, h, fmt, data = mip
    pixels = [None] * (w * h)
    block_bytes = 8 if fmt == "BC1" else 16
    bw = max(1, (w + 3) // 4)
    for index in range(len(data) // block_bytes):
        block = data[index * block_bytes:(index + 1) * block_bytes]
        color = block if fmt == "BC1" else block[8:]
        c0, c1, bits = struct.unpack("<HHI", color)
        # Writer always selects opaque four-colour BC1, including within BC3.
        assert c0 > c1
        p0, p1 = color565(c0), color565(c1)
        palette = [p0, p1, tuple((2*a+b)//3 for a, b in zip(p0, p1)),
                   tuple((a+2*b)//3 for a, b in zip(p0, p1))]
        alpha = [255] * 16
        if fmt == "BC3":
            a, b = block[:2]
            ap = [a, b]
            if a > b:
                ap += [((7-i)*a+i*b)//7 for i in range(1, 7)]
            else:
                ap += [((5-i)*a+i*b)//5 for i in range(1, 5)] + [0, 255]
            abits = int.from_bytes(block[2:8], "little")
            alpha = [ap[(abits >> (3*i)) & 7] for i in range(16)]
        for i in range(16):
            x, y = (index % bw)*4+i % 4, (index // bw)*4+i // 4
            if x < w and y < h:
                pixels[y*w+x] = (*palette[(bits >> (2*i)) & 3], alpha[i])
    assert all(p is not None for p in pixels)
    return pixels


def reference_mip(pixels, w, h):
    def linear(s):
        s /= 255
        return s / 12.92 if s <= .04045 else ((s+.055)/1.055)**2.4

    result = []
    for y in range(max(1, h // 2)):
        for x in range(max(1, w // 2)):
            samples = [pixels[min(h-1, 2*y+dy)*w+min(w-1, 2*x+dx)]
                       for dy in range(2) for dx in range(2)]
            alpha = sum(p[3] for p in samples)
            rgb = []
            for c in range(3):
                v = sum(linear(p[c])*p[3] for p in samples)/alpha if alpha else 0
                s = 12.92*v if v <= .0031308 else 1.055*v**(1/2.4)-.055
                rgb.append(max(0, min(255, int(s*255+.5))))
            result.append((*rgb, int(alpha/4+.5)))
    return result


class WriterTests(unittest.TestCase):
    def test_containers(self):
        for name in ("system_only.rsc", "page_growth.rsc", "large_page.rsc"):
            with self.subTest(name=name):
                Resource(OUTPUT / name)

    def test_dictionaries_and_compression(self):
        expected = {
            "opaque": (8, 4, [(32+x*24, 16+x*16, 8+x*8, 255) for _ in range(4) for x in range(8)]),
            "alpha": (4, 4, [(200, 40, 20, i*17) for i in range(16)]),
            "tiny": (1, 1, [(0, 255, 0, 255)]),
            "strip": (1, 8, [(40, 80, 120, 255)]*8),
            "transparent": (2, 2, [(200, 50, 100, 0)]*4),
        }
        dictionaries = []
        for filename in ("legacy.ytd", "enhanced.ytd", "textury_\u010d.ytd"):
            resource = Resource(OUTPUT / filename)
            textures = resource.textures()
            dictionaries.append(textures)
            self.assertEqual(set(textures), set(expected))
            cases = dict(expected)
            if resource.version == 13:
                cases.update(
                    tiny=(4, 4, [(0, 255, 0, 255)]*16),
                    strip=(4, 8, [(40, 80, 120, 255)]*32),
                    transparent=(4, 4, [(200, 50, 100, 0)]*16),
                )
            for name, (w, h, reference) in cases.items():
                mips = textures[name]
                count = int(math.log2(max(w, h))) + 1 if resource.version == 5 else int(math.log2(min(w, h))) - 1
                self.assertEqual(len(mips), count)
                for mip in mips:
                    with self.subTest(file=filename, name=name, size=(w, h)):
                        self.assertEqual(mip[:2], (w, h))
                        self.assertEqual(mip[2], "BC3" if name in ("alpha", "transparent") else "BC1")
                        pixels = decode(mip)
                        rgb_error = max(abs(p[c]-q[c]) for p, q in zip(pixels, reference) for c in range(3))
                        alpha_error = max(abs(p[3]-q[3]) for p, q in zip(pixels, reference))
                        self.assertLessEqual(rgb_error, 12)
                        self.assertLessEqual(alpha_error, 18)
                        reference = reference_mip(reference, w, h)
                        w, h = max(1, w//2), max(1, h//2)
        for name in ("opaque", "alpha"):
            self.assertEqual(dictionaries[0][name], dictionaries[1][name][:1])
        self.assertEqual(dictionaries[0], dictionaries[2])

    def test_sample_layouts(self):
        # Exercise this independent reader on game-format fixtures too.
        for generation in ("gen8", "gen9"):
            original = Resource(REPO / "test" / generation / "w_ar_carbinerifle_m31.ytd")
            textures = original.textures(strict=False)
            self.assertEqual(len(textures), 10)
            self.assertEqual(textures["w_ar_carbinerifle_m31"][0][:3], (1024, 1024, "BC1"))
            written = Resource(OUTPUT / f"{generation}_weapon.ytd").textures()
            self.assertEqual(len(written), 3)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: python tests\\verify_resources.py <repo-root> <generated-directory>")
    REPO, OUTPUT = map(Path, sys.argv[1:])
    unittest.main(argv=[sys.argv[0]])
