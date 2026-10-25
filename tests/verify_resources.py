"""Independent stdlib-only resource/import-project checks; run after ResourceWriterTests."""
import hashlib
import math
import struct
import sys
import unittest
import zlib
import xml.etree.ElementTree as ET
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


def read_shader(resource, shader):
    r = resource
    result = {"textures": {}, "vectors": {}}
    if r.version == 165:
        p = r.unpack("Q", shader)
        count, bucket, flag, size, data_size = r.unpack("BBHHH", shader + 16)
        result["name"] = r.unpack("I", shader + 8)
        result["bucket"] = bucket
        result["preset"] = r.unpack("I", shader + 24)
        result["mask"] = r.unpack("I", shader + 32)
        result["texture_count"] = r.unpack("B", shader + 39)
        types = [r.unpack("B", p + i*16) for i in range(count)]
        assert flag == 0x8000
        assert size == 16*count + 16*sum(types)
        assert data_size == (size + count*4 + 32 + 15) & ~15
        r.read(p, size + count*4 + 32 + data_size*4)
        hashes = [r.unpack("I", p + size + i*4) for i in range(count)]
        for i, (h, kind) in enumerate(zip(hashes, types)):
            pointer = r.unpack("Q", p + i*16 + 8)
            if kind == 0:
                result["textures"][h] = r.name(r.unpack("Q", pointer + 40)) if pointer else None
            else:
                assert kind == 1
                result["vectors"][h] = r.unpack("4f", pointer)
        return result

    assert r.version == 159
    result["name"], result["preset"] = r.unpack("II", shader)
    p, refs, unknowns, infos = r.unpack("4Q", shader + 8)
    nb, nt, nu, ns, count, zero1, zero2, copies = r.unpack("8B", infos)
    assert (nb, nt, nu, ns, count, zero1, zero2, copies) == (2, 4, 0, 5, 19, 0, 0, 12)
    assert unknowns == 0
    pointers = r.unpack("24Q", p)
    assert pointers[0] == p + nb*copies*8
    sizes = [pointers[1]-pointers[0], pointers[2]-pointers[1]]
    assert sizes == [32, 48]
    assert refs == p + nb*copies*8 + sum(sizes)*copies
    result["bucket"], size, result["mask"] = r.unpack("BHI", shader + 57)
    assert size == nb*copies*8 + sum(sizes)*copies + nt*copies*8 + ns
    r.read(p, size)
    assert r.read(refs + nt*copies*8, ns) == bytes(range(5))
    for copy in range(copies):
        for buffer in range(nb):
            pointer = pointers[copy*nb+buffer]
            assert pointer == pointers[buffer] + copy*sum(sizes)
            assert r.read(pointer, sizes[buffer]) == r.read(pointers[buffer], sizes[buffer])
        for slot in range(nt):
            if copy:
                assert r.unpack("Q", refs + (copy*nt+slot)*8) == 0
    metadata = []
    for i in range(count):
        h, descriptor = r.unpack("II", infos + 8 + i*8)
        metadata.append((h, descriptor))
        kind = descriptor & 3
        if kind == 0:
            slot = (descriptor >> 2) & 255
            assert slot < nt
            tex = r.unpack("Q", refs + slot*8)
            result["textures"][h] = r.name(r.unpack("Q", tex + 40)) if tex else None
        elif kind == 2:
            assert (descriptor >> 2) & 255 < ns
        else:
            assert kind == 3
            buffer = (descriptor >> 2) & 63
            offset, length = (descriptor >> 8) & 4095, descriptor >> 20
            assert buffer < nb and offset + length <= sizes[buffer] and length % 4 == 0
            values = r.unpack(f"{length//4}f", pointers[buffer] + offset)
            result["vectors"][h] = values if isinstance(values, tuple) else (values,)
    result["metadata"] = metadata
    return result


def read_drawable(resource):
    r = resource
    assert r.version in (165, 159)
    root = 0x50000000
    name = r.name(r.unpack("Q", root + 168))
    sphere = r.unpack("4f", root + 32)
    bbox = (r.unpack("3f", root + 48), r.unpack("3f", root + 64))
    assert r.unpack("I", root + 4) == 1
    assert r.unpack("Q", root + 24) == 0  # no skeleton
    assert r.unpack("3Q", root + 88) == (0, 0, 0)  # no additional LODs
    assert r.unpack("Q", root + 144) == 0  # no joints
    assert r.unpack("Q", root + 200) == 0  # no collision
    assert r.unpack("4f", root + 112) == (9998,)*4
    assert r.unpack("4I", root + 128) == (0xFF01, 0, 0, 0)
    group = r.unpack("Q", root + 16)
    assert r.unpack("Q", group + 8) == 0  # external YTD
    shader_array, shader_count, shader_capacity = r.unpack("QHH", group + 16)
    assert shader_count == shader_capacity
    shaders = [read_shader(r, r.unpack("Q", shader_array+i*8)) for i in range(shader_count)]
    high = r.unpack("Q", root + 80)
    pointers, count, capacity = r.unpack("QHH", high)
    assert count == capacity == 1
    model = r.unpack("Q", pointers)
    geometries, n, n2 = r.unpack("QHH", model + 8)
    assert n == n2 == r.unpack("H", model + 46)
    assert r.unpack("I", model + 40) == 0  # no skeleton binding
    assert r.unpack("H", model + 44) == 255
    bounds = r.unpack("Q", model + 24)
    shader_mapping = r.unpack("Q", model + 32)
    if n > 1:
        assert (r.unpack("3f", bounds), r.unpack("3f", bounds+16)) == bbox
    meshes = []
    for i in range(n):
        geometry = r.unpack("Q", geometries + i*8)
        vb, ib = r.unpack("Q", geometry+24), r.unpack("Q", geometry+56)
        indices, triangles, vertices, primitive = r.unpack("IIHH", geometry+88)
        assert triangles*3 == indices and primitive == 3
        assert r.unpack("H", geometry+112) == 52
        assert r.unpack("Q", geometry+104) == r.unpack("H", geometry+114) == 0
        assert r.unpack("I", ib+8) == indices
        if r.version == 165:
            stride = r.unpack("H", vb+8)
            data = r.unpack("Q", vb+16)
            assert r.unpack("I", vb+24) == vertices
            assert r.unpack("Q", vb+32) == data
            decl = r.unpack("Q", vb+48)
            flags, ds, unused, elements, types = r.unpack("IHBBQ", decl)
            assert (flags, ds, unused, elements) == (0x4059, 52, 0, 5)
            assert types == 6 | (6 << 12) | (9 << 16) | (5 << 24) | (7 << 56)
            index_data = r.unpack("Q", ib+16)
        else:
            vc, stride = r.unpack("IH", vb+8)
            assert vc == vertices and r.unpack("H", ib+12) == 2
            assert r.unpack("I", vb+16) == 0x580409
            assert r.unpack("I", ib+16) == 0x58020A
            data, index_data = r.unpack("Q", vb+24), r.unpack("Q", ib+24)
            decl = r.unpack("Q", vb+56)
            offsets, sizes, types = r.unpack("52I", decl), r.unpack("52B", decl+208), r.unpack("52B", decl+260)
            expected = {0: (0, 12, 6), 4: (12, 12, 6), 8: (36, 16, 2), 24: (24, 4, 28), 28: (28, 8, 16)}
            for slot in range(52):
                assert (offsets[slot], sizes[slot], types[slot]) == expected.get(slot, (0, 0, 0))
            assert r.unpack("Q", decl+312) == vertices << 10 | 52 << 2
            for buffer in (vb, ib):
                srv = r.unpack("Q", buffer+48)
                assert r.unpack("HHI", srv+16) == (0x14, 65535, 0xFFFFFFFF)
        assert stride == 52 and r.unpack("Q", geometry+120) == data
        decoded = [r.unpack("6f4B6f", data + v*52) for v in range(vertices)]
        indices = r.unpack(f"{indices}H", index_data)
        assert min(indices) >= 0 and max(indices) < vertices
        shader_id = r.unpack("H", shader_mapping + i*2)
        assert shader_id < len(shaders)
        box_ptr = bounds + (0 if n == 1 else i+1)*32
        box = (r.unpack("3f", box_ptr), r.unpack("3f", box_ptr+16))
        for v in decoded:
            for k in range(3):
                assert box[0][k] <= v[k] <= box[1][k]
            assert math.dist(v[:3], sphere[:3]) <= sphere[3]+1e-6
        meshes.append((decoded, indices, shader_id, box))
    return name, bbox, sphere, shaders, meshes


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

    def test_import_projects(self):
        for filename in ("mapping.vwtimport.xml", "m4a1_preparation.vwtimport.xml"):
            path = OUTPUT / filename
            if not path.exists() and filename.startswith("m4a1"):
                continue
            with self.subTest(project=filename):
                root = ET.parse(path).getroot()
                self.assertEqual(root.tag, "glb-import")
                self.assertEqual(root.attrib["version"], "1")
                self.assertEqual(root.attrib["template"], "WEAPON_CARBINERIFLE")
                source = (path.parent / root.attrib["source"]).resolve()
                if not source.exists() and filename.startswith("m4a1"):
                    continue
                raw = source.read_bytes()
                self.assertEqual(hashlib.sha256(raw).hexdigest(), root.attrib["sha256"])
                parts = list(root)
                self.assertEqual([int(p.attrib["index"]) for p in parts], list(range(len(parts))))
                identities = [(int(p.attrib["node"]), int(p.attrib["primitive"])) for p in parts]
                self.assertEqual(len(set(identities)), len(parts))
                self.assertTrue(all(p.tag == "part" for p in parts))
                self.assertIn("body", [p.attrib["role"] for p in parts])
                if filename == "mapping.vwtimport.xml":
                    self.assertEqual([p.attrib["role"] for p in parts], ["body", "excluded"])
                else:
                    self.assertEqual(len(parts), 15)

    def test_written_drawables(self):
        # The supplied shader provides an independent reference for the gen9 binding table.
        fixture = Resource(REPO / "test" / "gen9" / "w_ar_carbinerifle_m31.ydr")
        group = fixture.unpack("Q", 0x50000010)
        shader = fixture.unpack("Q", fixture.unpack("Q", group+16))
        profile = read_shader(fixture, shader)
        for generation in ("gen8", "gen9"):
            for variant in ("static", "single", "boundary"):
                with self.subTest(generation=generation, variant=variant):
                    r = Resource(OUTPUT / f"{generation}_{variant}.ydr")
                    name, bbox, sphere, shaders, meshes = read_drawable(r)
                    self.assertEqual(name, "w_test_static.#dr")
                    self.assertEqual(len(meshes), 2 if variant == "static" else 1)
                    self.assertEqual(bbox, ((-2., -1., 0.), (3., 1., 1.)) if variant == "static"
                                     else ((-2., -1., 0.), (0., 1., 0.)))
                    for i, material in enumerate(shaders):
                        self.assertEqual(material["name"], joaat("normal_spec_decal"))
                        self.assertEqual((material["bucket"], material["mask"]), (2, 0xFF04))
                        diffuse_hash = 0xC8E8C282 if generation == "gen9" else joaat("diffusesampler")
                        self.assertEqual(material["textures"][diffuse_hash], "alpha" if i else "opaque")
                        normal_hash = 0x1E77BF4C if generation == "gen9" else joaat("bumpsampler")
                        specular_hash = 0xC6F46E79 if generation == "gen9" else joaat("specsampler")
                        self.assertEqual(material["textures"][normal_hash], "flat_normal")
                        self.assertEqual(material["textures"][specular_hash], "black_spec")
                        if generation == "gen9":
                            self.assertEqual(material["metadata"], profile["metadata"])
                            self.assertIsNone(material["textures"][0x8E84DF1F])
                            values = material["vectors"]
                            self.assertAlmostEqual(values[0xB5FE3ACB][0], .75 if i else .99, places=6)
                            self.assertAlmostEqual(values[0xA1F60489][0], 40 if i else 20)
                            self.assertAlmostEqual(values[0xA98E2243][0], .5 if i else .1, places=6)
                        else:
                            self.assertEqual(material["preset"], joaat("normal_spec_decal.sps"))
                            self.assertEqual(material["texture_count"], 3)
                            self.assertEqual(len(material["vectors"]), 9)
                            values = material["vectors"]
                            self.assertAlmostEqual(values[joaat("specularfresnel")][0], .75 if i else .99, places=6)
                            self.assertAlmostEqual(values[joaat("specularfalloffmult")][0], 40 if i else 20)
                            self.assertAlmostEqual(values[joaat("specularintensitymult")][0], .5 if i else .1, places=6)
                        values = material["vectors"]
                        self.assertAlmostEqual(values[joaat("wetnessmultiplier")][0], .25 if i else 1)
                        self.assertAlmostEqual(values[joaat("bumpiness")][0], .7 if i else 1, places=6)
                        self.assertEqual(values[joaat("usetessellation")][0], 0)
                        self.assertEqual(values[joaat("globalanimuv0")][:3], (1., 0., 0.))
                        self.assertEqual(values[joaat("globalanimuv1")][:3], (0., 1., 0.))
                    for i, (vertices, indices, shader_id, _) in enumerate(meshes):
                        self.assertEqual(shader_id, i)
                        self.assertEqual(len(vertices), 65535 if variant == "boundary" else 4)
                        self.assertEqual(indices, (0, 1, 65534) if variant == "boundary" else
                                         (0, 2, 1, 0, 3, 2) if i else (0, 1, 2, 0, 2, 3))
                        positions = [(-2., -1., 0.), (0., -1., 0.), (0., 1., 0.), (-2., 1., 0.)]
                        coords = [(0., 0.), (1., 0.), (1., 1.), (0., 1.)]
                        selected = [0, 1, 2, 3, 65534] if variant == "boundary" else range(4)
                        for v in selected if not i else ():
                            vertex = vertices[v]
                            source = 2 if v == 65534 else v
                            self.assertEqual(vertex[:3], positions[source])
                            self.assertEqual(vertex[3:6], (0., 0., 1.))
                            self.assertEqual(vertex[6:10], (128, 64, 32, 255))
                            self.assertEqual(vertex[12:], (1., 0., 0., 1.))
                            self.assertEqual(vertex[10:12], coords[source])
                        if i:
                            for v, vertex in enumerate(vertices):
                                self.assertEqual(vertex[:3], (positions[v][0]+3, positions[v][1], 1.))
                                self.assertEqual(vertex[3:6], (0., 0., 1.))
                                self.assertEqual(vertex[6:10], (25, 80, 150, 200))
                                self.assertEqual(vertex[10:12], (coords[v][0]-.25, coords[v][1]*2))
                                self.assertEqual(vertex[12:], (-1., 0., 0., -1.))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: python tests\\verify_resources.py <repo-root> <generated-directory>")
    REPO, OUTPUT = map(Path, sys.argv[1:])
    unittest.main(argv=[sys.argv[0]])
