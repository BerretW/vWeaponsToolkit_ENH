#include "cModelPreview.h"
#include <wx/dcbuffer.h>
#include <wx/mstream.h>
#include <wx/file.h>
#include <wx/zstream.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

namespace
{
	const uint32_t kDiffuseSamplerHash = 0xf1fe2b71;

	struct Rsc
	{
		std::vector<uint8_t> d;
		uint32_t ssize = 0;
		uint32_t version = 0;

		static uint32_t sizeFromFlags(uint32_t f)
		{
			uint32_t s = ((f >> 27) & 1) + (((f >> 26) & 1) << 1) + (((f >> 25) & 1) << 2) + (((f >> 24) & 1) << 3)
				+ (((f >> 17) & 0x7F) << 4) + (((f >> 11) & 0x3F) << 5) + (((f >> 7) & 0xF) << 6)
				+ (((f >> 5) & 3) << 7) + (((f >> 4) & 1) << 8);
			return (0x200u << (f & 0xF)) * s;
		}

		bool load(const std::string& path, std::string& err)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in) { err = "cannot open " + path; return false; }
			std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (raw.size() < 16 || std::memcmp(raw.data(), "RSC7", 4) != 0) { err = "not an RSC7 resource: " + path; return false; }
			uint32_t sf, gf;
			std::memcpy(&sf, &raw[8], 4);
			std::memcpy(&gf, &raw[12], 4);
			version = ((sf >> 28) << 4) | (gf >> 28);
			ssize = sizeFromFlags(sf);
			size_t expected = (size_t)ssize + sizeFromFlags(gf);
			if (expected == 0 || expected > 0x20000000) { err = "invalid resource size"; return false; }

			wxMemoryInputStream mem(raw.data() + 16, raw.size() - 16);
			wxZlibInputStream zs(mem, wxZLIB_NO_HEADER);
			d.resize(expected);
			size_t got = 0;
			while (got < expected)
			{
				zs.Read(d.data() + got, expected - got);
				size_t n = zs.LastRead();
				if (n == 0) break;
				got += n;
			}
			if (got < ssize) { err = "failed to inflate " + path; return false; }
			d.resize(got);
			return true;
		}

		size_t off(uint64_t p, size_t need = 1) const
		{
			size_t o;
			uint64_t hi = p & 0xF0000000;
			if (hi == 0x50000000) o = (size_t)(p & 0x0FFFFFFF);
			else if (hi == 0x60000000) o = (size_t)ssize + (size_t)(p & 0x0FFFFFFF);
			else throw std::runtime_error("bad pointer");
			if (o + need > d.size() || o + need < o) throw std::runtime_error("pointer out of range");
			return o;
		}

		template <class T> T get(uint64_t p) const
		{
			T v;
			std::memcpy(&v, &d[off(p, sizeof(T))], sizeof(T));
			return v;
		}
		uint8_t u8(uint64_t p) const { return get<uint8_t>(p); }
		uint16_t u16(uint64_t p) const { return get<uint16_t>(p); }
		uint32_t u32(uint64_t p) const { return get<uint32_t>(p); }
		uint64_t u64(uint64_t p) const { return get<uint64_t>(p); }
		const uint8_t* ptr(uint64_t p, size_t need) const { return &d[off(p, need)]; }

		std::string cstr(uint64_t p) const
		{
			size_t o = off(p);
			std::string s;
			while (o < d.size() && d[o] && s.size() < 256) s.push_back((char)d[o++]);
			return s;
		}
	};

	std::string lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	bool endsWith(const std::string& s, const char* suffix)
	{
		size_t n = std::strlen(suffix);
		return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
	}

	float half2f(uint16_t h)
	{
		int s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
		float f;
		if (e == 0) f = std::ldexp((float)m, -24);
		else if (e == 31) f = m ? 0.0f : 65504.0f;
		else f = std::ldexp((float)(m | 1024), e - 25);
		return s ? -f : f;
	}

	// ---------- texture decoding ----------

	enum class Fmt { Unknown, BC1, BC2, BC3, BC4, BC5, BC7, RGBA8, BGRA8, L8 };

	int blockBytes(Fmt f) { return (f == Fmt::BC1 || f == Fmt::BC4) ? 8 : 16; }
	bool isBlock(Fmt f) { return f == Fmt::BC1 || f == Fmt::BC2 || f == Fmt::BC3 || f == Fmt::BC4 || f == Fmt::BC5 || f == Fmt::BC7; }
	int pixelBytes(Fmt f) { return f == Fmt::L8 ? 1 : 4; }

	size_t levelSize(Fmt f, int w, int h)
	{
		if (isBlock(f)) return (size_t)std::max(1, (w + 3) / 4) * std::max(1, (h + 3) / 4) * blockBytes(f);
		return (size_t)w * h * pixelBytes(f);
	}

	Fmt fmtFromLegacy(uint32_t f)
	{
		switch (f)
		{
		case 0x31545844: return Fmt::BC1;
		case 0x33545844: return Fmt::BC2;
		case 0x35545844: return Fmt::BC3;
		case 0x31495441: return Fmt::BC4;
		case 0x32495441: return Fmt::BC5;
		case 0x20374342: return Fmt::BC7;
		case 21: case 22: return Fmt::BGRA8;
		case 32: return Fmt::RGBA8;
		case 28: case 50: return Fmt::L8;
		}
		return Fmt::Unknown;
	}

	Fmt fmtFromDxgi(uint32_t f)
	{
		switch (f)
		{
		case 71: case 72: return Fmt::BC1;
		case 74: case 75: return Fmt::BC2;
		case 77: case 78: return Fmt::BC3;
		case 80: case 81: return Fmt::BC4;
		case 83: case 84: return Fmt::BC5;
		case 98: case 99: return Fmt::BC7;
		case 28: case 29: return Fmt::RGBA8;
		case 87: case 91: return Fmt::BGRA8;
		case 61: case 65: return Fmt::L8;
		}
		return Fmt::Unknown;
	}

	void c565(uint16_t c, uint8_t* o)
	{
		o[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
		o[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
		o[2] = (uint8_t)((c & 31) * 255 / 31);
	}

	void decodeBC1Color(const uint8_t* b, uint8_t px[16][4], bool allowAlpha)
	{
		uint16_t c0, c1;
		std::memcpy(&c0, b, 2);
		std::memcpy(&c1, b + 2, 2);
		uint8_t col[4][4];
		c565(c0, col[0]); c565(c1, col[1]);
		col[0][3] = col[1][3] = col[2][3] = col[3][3] = 255;
		if (c0 > c1 || !allowAlpha)
		{
			for (int k = 0; k < 3; k++)
			{
				col[2][k] = (uint8_t)((2 * col[0][k] + col[1][k]) / 3);
				col[3][k] = (uint8_t)((col[0][k] + 2 * col[1][k]) / 3);
			}
		}
		else
		{
			for (int k = 0; k < 3; k++)
			{
				col[2][k] = (uint8_t)((col[0][k] + col[1][k]) / 2);
				col[3][k] = 0;
			}
			col[3][3] = 0;
		}
		uint32_t bits;
		std::memcpy(&bits, b + 4, 4);
		for (int i = 0; i < 16; i++)
			std::memcpy(px[i], col[(bits >> (2 * i)) & 3], 4);
	}

	void decodeAlphaBlock(const uint8_t* b, uint8_t out[16], bool signedMode = false)
	{
		uint8_t a[8];
		a[0] = b[0]; a[1] = b[1];
		if (a[0] > a[1])
			for (int i = 1; i < 7; i++) a[i + 1] = (uint8_t)(((7 - i) * a[0] + i * a[1]) / 7);
		else
		{
			for (int i = 1; i < 5; i++) a[i + 1] = (uint8_t)(((5 - i) * a[0] + i * a[1]) / 5);
			a[6] = 0; a[7] = 255;
		}
		uint64_t bits = 0;
		for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
		for (int i = 0; i < 16; i++) out[i] = a[(bits >> (3 * i)) & 7];
		(void)signedMode;
	}

	// BC7: only mode 6 (single subset, 7-bit endpoints + p-bit) is decoded; other modes fall back to mid gray.
	void decodeBC7(const uint8_t* b, uint8_t px[16][4])
	{
		int bitPos = 0;
		auto rd = [&](int n) { uint32_t v = 0; for (int i = 0; i < n; i++) { v |= (uint32_t)((b[bitPos >> 3] >> (bitPos & 7)) & 1) << i; bitPos++; } return v; };
		int mode = 0;
		while (mode < 8 && rd(1) == 0) mode++;
		if (mode != 6)
		{
			for (int i = 0; i < 16; i++) { px[i][0] = px[i][1] = px[i][2] = 128; px[i][3] = 255; }
			return;
		}
		uint32_t ep[2][4];
		for (int c = 0; c < 4; c++) { ep[0][c] = rd(7); ep[1][c] = rd(7); }
		uint32_t p0 = rd(1), p1 = rd(1);
		for (int c = 0; c < 4; c++) { ep[0][c] = (ep[0][c] << 1) | p0; ep[1][c] = (ep[1][c] << 1) | p1; }
		static const int w4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
		for (int i = 0; i < 16; i++)
		{
			int n = (i == 0) ? 3 : 4;
			int idx = (int)rd(n);
			for (int c = 0; c < 4; c++)
				px[i][c] = (uint8_t)(((64 - w4[idx]) * ep[0][c] + w4[idx] * ep[1][c] + 32) >> 6);
		}
	}

	bool decodeTexture(Fmt fmt, int w, int h, const uint8_t* src, size_t srcSize, PreviewTexture& out)
	{
		if (w <= 0 || h <= 0 || fmt == Fmt::Unknown || srcSize < levelSize(fmt, w, h)) return false;
		out.width = w; out.height = h;
		out.rgba.assign((size_t)w * h * 4, 255);
		auto put = [&](int x, int y, const uint8_t* c)
		{
			if (x < w && y < h) std::memcpy(&out.rgba[((size_t)y * w + x) * 4], c, 4);
		};
		if (isBlock(fmt))
		{
			int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
			for (int by = 0; by < bh; by++)
				for (int bx = 0; bx < bw; bx++)
				{
					const uint8_t* b = src + ((size_t)by * bw + bx) * blockBytes(fmt);
					uint8_t px[16][4];
					switch (fmt)
					{
					case Fmt::BC1: decodeBC1Color(b, px, true); break;
					case Fmt::BC2:
						decodeBC1Color(b + 8, px, false);
						for (int i = 0; i < 16; i++) { int a = (b[i / 2] >> ((i & 1) * 4)) & 15; px[i][3] = (uint8_t)(a * 17); }
						break;
					case Fmt::BC3:
					{
						decodeBC1Color(b + 8, px, false);
						uint8_t a[16];
						decodeAlphaBlock(b, a);
						for (int i = 0; i < 16; i++) px[i][3] = a[i];
						break;
					}
					case Fmt::BC4:
					{
						uint8_t r[16];
						decodeAlphaBlock(b, r);
						for (int i = 0; i < 16; i++) { px[i][0] = px[i][1] = px[i][2] = r[i]; px[i][3] = 255; }
						break;
					}
					case Fmt::BC5:
					{
						uint8_t r[16], g[16];
						decodeAlphaBlock(b, r);
						decodeAlphaBlock(b + 8, g);
						for (int i = 0; i < 16; i++) { px[i][0] = r[i]; px[i][1] = g[i]; px[i][2] = 128; px[i][3] = 255; }
						break;
					}
					default: decodeBC7(b, px); break;
					}
					for (int i = 0; i < 16; i++) put(bx * 4 + (i & 3), by * 4 + (i >> 2), px[i]);
				}
		}
		else
		{
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
				{
					uint8_t c[4] = { 255, 255, 255, 255 };
					if (fmt == Fmt::L8) c[0] = c[1] = c[2] = src[(size_t)y * w + x];
					else
					{
						const uint8_t* s = src + ((size_t)y * w + x) * 4;
						if (fmt == Fmt::BGRA8) { c[0] = s[2]; c[1] = s[1]; c[2] = s[0]; c[3] = s[3]; }
						else std::memcpy(c, s, 4);
					}
					put(x, y, c);
				}
		}
		return true;
	}

	struct YtdTexture
	{
		const Rsc* rsc = nullptr;
		bool gen9 = false;
		int w = 0, h = 0, levels = 1;
		uint32_t format = 0;
		uint64_t data = 0;
	};

	void readYtd(const Rsc& r, std::map<std::string, YtdTexture>& textures)
	{
		bool gen9 = r.version == 5;
		uint64_t base = 0x50000000;
		uint64_t arr = r.u64(base + 0x30);
		uint16_t count = r.u16(base + 0x38);
		if (!arr) return;
		for (uint16_t i = 0; i < count; i++)
		{
			uint64_t t = r.u64(arr + 8ull * i);
			if (!t) continue;
			YtdTexture tx;
			tx.rsc = &r;
			tx.gen9 = gen9;
			uint64_t namePtr = r.u64(t + 0x28);
			if (!namePtr) continue;
			std::string name = lower(r.cstr(namePtr));
			if (gen9)
			{
				tx.w = r.u16(t + 0x18); tx.h = r.u16(t + 0x1A);
				tx.format = r.u8(t + 0x1F); tx.levels = r.u8(t + 0x22); tx.data = r.u64(t + 0x38);
			}
			else
			{
				tx.w = r.u16(t + 0x50); tx.h = r.u16(t + 0x52);
				tx.format = r.u32(t + 0x58); tx.levels = r.u8(t + 0x5D); tx.data = r.u64(t + 0x70);
			}
			if (textures.find(name) == textures.end()) textures[name] = tx;
		}
	}

	bool decodeYtdTexture(const YtdTexture& t, PreviewTexture& out)
	{
		Fmt fmt = t.gen9 ? fmtFromDxgi(t.format) : fmtFromLegacy(t.format);
		if (fmt == Fmt::Unknown || !t.data) return false;
		int w = t.w, h = t.h;
		size_t off = 0;
		int levels = std::max(1, t.levels);
		for (int l = 0; l + 1 < levels && std::max(w, h) > 1024; l++)
		{
			off += levelSize(fmt, w, h);
			w = std::max(1, w / 2);
			h = std::max(1, h / 2);
		}
		size_t need = levelSize(fmt, w, h);
		const Rsc& r = *t.rsc;
		size_t o = r.off(t.data + off, need);
		return decodeTexture(fmt, w, h, &r.d[o], need, out);
	}

	// ---------- drawable parsing ----------

	struct ShaderTextures
	{
		std::vector<std::pair<uint32_t, std::string>> params;
	};

	std::string pickDiffuse(const ShaderTextures& s)
	{
		for (auto& p : s.params)
			if (p.first == kDiffuseSamplerHash && !p.second.empty()) return p.second;
		for (auto& p : s.params)
		{
			std::string n = lower(p.second);
			if (n.empty()) continue;
			if (endsWith(n, "_n") || endsWith(n, "_s") || endsWith(n, "_nm") || endsWith(n, "_dpal") || n.compare(0, 4, "env_") == 0) continue;
			return p.second;
		}
		for (auto& p : s.params)
			if (!p.second.empty()) return p.second;
		return "";
	}

	std::string textureNameAt(const Rsc& r, uint64_t tex)
	{
		if (!tex) return "";
		uint64_t np = r.u64(tex + 0x28);
		return np ? r.cstr(np) : "";
	}

	std::vector<ShaderTextures> readShaders(const Rsc& r, bool gen9)
	{
		std::vector<ShaderTextures> out;
		uint64_t sg = r.u64(0x50000010);
		if (!sg) return out;
		uint64_t sp = r.u64(sg + 0x10);
		uint16_t sc = r.u16(sg + 0x18);
		if (!sp) return out;
		for (uint16_t i = 0; i < sc; i++)
		{
			ShaderTextures st;
			try
			{
				uint64_t sh = r.u64(sp + 8ull * i);
				if (!gen9)
				{
					uint64_t pp = r.u64(sh);
					int cnt = r.u8(sh + 0x10);
					std::vector<int> types(cnt);
					uint64_t offset = 16ull * cnt;
					for (int k = 0; k < cnt; k++) { types[k] = r.u8(pp + 16ull * k); offset += 16ull * types[k]; }
					for (int k = 0; k < cnt; k++)
						if (types[k] == 0)
							st.params.push_back({ r.u32(pp + offset + 4ull * k), textureNameAt(r, r.u64(pp + 16ull * k + 8)) });
				}
				else
				{
					uint64_t refs = r.u64(sh + 0x10), infos = r.u64(sh + 0x20);
					int npar = r.u8(infos + 4);
					for (int k = 0; k < npar; k++)
					{
						uint32_t h = r.u32(infos + 8 + 8ull * k), d = r.u32(infos + 12 + 8ull * k);
						if ((d & 3) == 0)
							st.params.push_back({ h, textureNameAt(r, r.u64(refs + 8ull * ((d >> 2) & 0xFF))) });
					}
				}
			}
			catch (const std::exception&) {}
			out.push_back(st);
		}
		return out;
	}

	bool readGeometry(const Rsc& r, bool gen9, uint64_t g, PreviewMesh& m)
	{
		uint64_t vb = r.u64(g + 0x18), ib = r.u64(g + 0x38);
		if (!vb || !ib) return false;
		uint32_t vcount, stride;
		uint64_t data;
		int posOff = -1, nrmOff = -1, uvOff = -1;
		bool uvHalf = false;
		uint32_t icount;
		uint64_t iptr;
		if (gen9)
		{
			vcount = r.u32(vb + 8); stride = r.u16(vb + 0xC); data = r.u64(vb + 0x18);
			uint64_t info = r.u64(vb + 0x38);
			uint32_t offs[52]; uint8_t types[52];
			std::memcpy(offs, r.ptr(info, 208), 208);
			std::memcpy(types, r.ptr(info + 260, 52), 52);
			if (types[0] == 6) posOff = (int)offs[0];
			if (types[4] == 6) nrmOff = (int)offs[4];
			if (types[28] == 16) uvOff = (int)offs[28];
			else if (types[28] == 34) { uvOff = (int)offs[28]; uvHalf = true; }
			icount = r.u32(ib + 8); iptr = r.u64(ib + 0x18);
		}
		else
		{
			stride = r.u16(vb + 8); data = r.u64(vb + 0x10); vcount = r.u32(vb + 0x18);
			uint64_t info = r.u64(vb + 0x30);
			uint32_t flags = r.u32(info);
			uint64_t types = r.u64(info + 8);
			static const int sizes[16] = { 0, 4, 4, 8, 4, 8, 12, 16, 4, 4, 4, 4, 4, 4, 4, 4 };
			int o = 0;
			for (int k = 0; k < 16; k++)
			{
				if (!((flags >> k) & 1)) continue;
				int t = (int)((types >> (4 * k)) & 0xF);
				if (k == 0 && t == 6) posOff = o;
				if (k == 3 && t == 6) nrmOff = o;
				if (k == 6 && t == 5) uvOff = o;
				if (k == 6 && t == 1) { uvOff = o; uvHalf = true; }
				o += sizes[t];
			}
			icount = r.u32(ib + 8); iptr = r.u64(ib + 0x10);
		}
		if (posOff < 0 || !vcount || !stride || !data || !iptr || icount < 3 || vcount > 200000 || icount > 3000000) return false;

		const uint8_t* vd = r.ptr(data, (size_t)vcount * stride);
		const uint8_t* id = r.ptr(iptr, (size_t)icount * 2);
		m.pos.resize((size_t)vcount * 3);
		m.uv.assign((size_t)vcount * 2, 0.0f);
		if (nrmOff >= 0) m.nrm.resize((size_t)vcount * 3);
		for (uint32_t v = 0; v < vcount; v++)
		{
			const uint8_t* p = vd + (size_t)v * stride;
			std::memcpy(&m.pos[v * 3], p + posOff, 12);
			if (nrmOff >= 0) std::memcpy(&m.nrm[v * 3], p + nrmOff, 12);
			if (uvOff >= 0)
			{
				if (uvHalf)
				{
					uint16_t h[2];
					std::memcpy(h, p + uvOff, 4);
					m.uv[v * 2] = half2f(h[0]); m.uv[v * 2 + 1] = half2f(h[1]);
				}
				else std::memcpy(&m.uv[v * 2], p + uvOff, 8);
			}
		}
		m.idx.reserve(icount);
		for (uint32_t i = 0; i + 2 < icount; i += 3)
		{
			uint16_t a, b, c;
			std::memcpy(&a, id + i * 2, 2); std::memcpy(&b, id + i * 2 + 2, 2); std::memcpy(&c, id + i * 2 + 4, 2);
			if (a < vcount && b < vcount && c < vcount && a != b && b != c && a != c)
			{
				m.idx.push_back(a); m.idx.push_back(b); m.idx.push_back(c);
			}
		}
		return !m.idx.empty();
	}
}

bool loadPreviewModel(const std::string& ydrPath, const std::vector<std::string>& ytdPaths, PreviewModel& out, std::string& error)
{
	out = PreviewModel();
	try
	{
		Rsc ydr;
		if (!ydr.load(ydrPath, error)) return false;
		bool gen9 = ydr.version == 159;
		if (!gen9 && ydr.version != 165) { error = "unsupported .ydr version " + std::to_string(ydr.version); return false; }

		std::vector<Rsc> ytds(ytdPaths.size());
		std::map<std::string, YtdTexture> available;
		int ytdLoaded = 0;
		for (size_t i = 0; i < ytdPaths.size(); i++)
		{
			std::string e;
			if (!ytds[i].load(ytdPaths[i], e)) continue;
			try { readYtd(ytds[i], available); ytdLoaded++; }
			catch (const std::exception&) {}
		}

		std::vector<ShaderTextures> shaders = readShaders(ydr, gen9);
		std::map<std::string, int> loadedTextures;
		int missingTextures = 0;

		const uint64_t base = 0x50000000;
		const uint64_t lods[4] = { 0x50, 0x58, 0x60, 0x68 };
		for (uint64_t lod : lods)
		{
			uint64_t lp = ydr.u64(base + lod);
			if (!lp) continue;
			uint64_t arr = ydr.u64(lp);
			uint16_t count = ydr.u16(lp + 8);
			if (!arr || !count) continue;
			for (uint16_t mi = 0; mi < count; mi++)
			{
				uint64_t mp = ydr.u64(arr + 8ull * mi);
				if (!mp) continue;
				uint64_t gptr = ydr.u64(mp + 8);
				uint16_t gcount = ydr.u16(mp + 0x10);
				uint64_t smap = ydr.u64(mp + 0x20);
				for (uint16_t gi = 0; gi < gcount; gi++)
				{
					PreviewMesh mesh;
					try
					{
						if (!readGeometry(ydr, gen9, ydr.u64(gptr + 8ull * gi), mesh)) continue;
					}
					catch (const std::exception&) { continue; }

					if (smap)
					{
						try
						{
							uint16_t si = ydr.u16(smap + 2ull * gi);
							if (si < shaders.size())
							{
								std::string name = lower(pickDiffuse(shaders[si]));
								auto cached = loadedTextures.find(name);
								if (cached != loadedTextures.end()) mesh.texture = cached->second;
								else
								{
									int index = -1;
									auto it = available.find(name);
									PreviewTexture pt;
									if (it != available.end() && decodeYtdTexture(it->second, pt))
									{
										index = (int)out.textures.size();
										out.textures.push_back(std::move(pt));
									}
									else if (!name.empty()) missingTextures++;
									loadedTextures[name] = index;
									mesh.texture = index;
								}
							}
						}
						catch (const std::exception&) {}
					}
					out.meshes.push_back(std::move(mesh));
				}
			}
			if (!out.meshes.empty()) break;
		}
		if (out.meshes.empty()) { error = "no renderable geometry found"; return false; }

		bool first = true;
		size_t tris = 0;
		for (auto& m : out.meshes)
		{
			tris += m.idx.size() / 3;
			for (size_t v = 0; v + 2 < m.pos.size(); v += 3)
				for (int k = 0; k < 3; k++)
				{
					float x = m.pos[v + k];
					if (first) out.bmin[k] = out.bmax[k] = x;
					out.bmin[k] = std::min(out.bmin[k], x);
					out.bmax[k] = std::max(out.bmax[k], x);
				}
			first = false;
		}
		out.info = std::string(gen9 ? "Enhanced (gen9)" : "Legacy (gen8)") + ", " + std::to_string(out.meshes.size()) + " meshes, "
			+ std::to_string(tris) + " tris, " + std::to_string(out.textures.size()) + " textures";
		if (ytdPaths.empty() || ytdLoaded == 0) out.info += " (no .ytd)";
		else if (missingTextures) out.info += " (" + std::to_string(missingTextures) + " missing)";
		return true;
	}
	catch (const std::exception& e)
	{
		error = std::string("parse error: ") + e.what();
		return false;
	}
}

// ---------- software renderer ----------

static wxImage renderModel(const PreviewModel& m, int W, int H, float yaw, float pitch, float zoom)
{
	wxImage img(W, H, false);
	uint8_t* out = img.GetData();
	for (int y = 0; y < H; y++)
	{
		uint8_t g = (uint8_t)(70 - 35 * y / std::max(1, H));
		for (int x = 0; x < W; x++) { uint8_t* p = out + ((size_t)y * W + x) * 3; p[0] = g; p[1] = g; p[2] = (uint8_t)(g + 8); }
	}

	float c[3], radius = 0;
	for (int k = 0; k < 3; k++) c[k] = (m.bmin[k] + m.bmax[k]) * 0.5f;
	for (int k = 0; k < 3; k++) { float e = (m.bmax[k] - m.bmin[k]) * 0.5f; radius += e * e; }
	radius = std::max(std::sqrt(radius), 1e-4f);
	float scale = std::min(W, H) * 0.5f / radius * 0.95f * zoom;

	float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
	auto rot = [&](float x, float y, float z, float* o)
	{
		float x1 = x * cy - y * sy, y1 = x * sy + y * cy;
		o[0] = x1; o[1] = y1 * cp - z * sp; o[2] = y1 * sp + z * cp;
	};

	std::vector<float> zbuf((size_t)W * H, 1e30f);
	const float L[3] = { -0.35f, -0.6f, 0.72f };

	for (const PreviewMesh& mesh : m.meshes)
	{
		size_t vc = mesh.pos.size() / 3;
		std::vector<float> sx(vc), sy2(vc), sz(vc), nrm(vc * 3);
		for (size_t v = 0; v < vc; v++)
		{
			float r[3];
			rot(mesh.pos[v * 3] - c[0], mesh.pos[v * 3 + 1] - c[1], mesh.pos[v * 3 + 2] - c[2], r);
			sx[v] = W * 0.5f + r[0] * scale;
			sy2[v] = H * 0.5f - r[2] * scale;
			sz[v] = r[1];
			if (!mesh.nrm.empty()) rot(mesh.nrm[v * 3], mesh.nrm[v * 3 + 1], mesh.nrm[v * 3 + 2], &nrm[v * 3]);
		}
		const PreviewTexture* tex = (mesh.texture >= 0 && mesh.texture < (int)m.textures.size()) ? &m.textures[mesh.texture] : nullptr;

		for (size_t t = 0; t + 2 < mesh.idx.size(); t += 3)
		{
			uint32_t i0 = mesh.idx[t], i1 = mesh.idx[t + 1], i2 = mesh.idx[t + 2];
			float x0 = sx[i0], y0 = sy2[i0], x1 = sx[i1], y1 = sy2[i1], x2 = sx[i2], y2 = sy2[i2];
			float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
			if (std::fabs(area) < 1e-6f) continue;

			float fn[3] = { 0, 0, 0 };
			if (mesh.nrm.empty())
			{
				float ax = x1 - x0, ay = y1 - y0, az = sz[i1] - sz[i0];
				float bx = x2 - x0, by = y2 - y0, bz = sz[i2] - sz[i0];
				fn[0] = ay * bz - az * by; fn[1] = az * bx - ax * bz; fn[2] = ax * by - ay * bx;
				float len = std::sqrt(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
				if (len > 0) { fn[0] /= len; fn[1] /= len; fn[2] /= len; }
			}

			int minX = std::max(0, (int)std::floor(std::min({ x0, x1, x2 })));
			int maxX = std::min(W - 1, (int)std::ceil(std::max({ x0, x1, x2 })));
			int minY = std::max(0, (int)std::floor(std::min({ y0, y1, y2 })));
			int maxY = std::min(H - 1, (int)std::ceil(std::max({ y0, y1, y2 })));
			float inv = 1.0f / area;

			for (int y = minY; y <= maxY; y++)
				for (int x = minX; x <= maxX; x++)
				{
					float px = x + 0.5f, py = y + 0.5f;
					float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) * inv;
					float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) * inv;
					float w2 = 1.0f - w0 - w1;
					if (w0 < 0 || w1 < 0 || w2 < 0) continue;
					float z = w0 * sz[i0] + w1 * sz[i1] + w2 * sz[i2];
					float& zb = zbuf[(size_t)y * W + x];
					if (z >= zb) continue;

					float n[3];
					if (mesh.nrm.empty()) { n[0] = fn[0]; n[1] = fn[1]; n[2] = fn[2]; }
					else for (int k = 0; k < 3; k++) n[k] = w0 * nrm[i0 * 3 + k] + w1 * nrm[i1 * 3 + k] + w2 * nrm[i2 * 3 + k];
					float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
					float shade = 0.75f;
					if (nl > 1e-6f) shade = 0.35f + 0.65f * std::fabs((n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / nl);

					const float base = mesh.colors.empty() ? 190.0f : 255.0f;
					float rgb[3] = { base, base, base };
					if (tex)
					{
						float u = w0 * mesh.uv[i0 * 2] + w1 * mesh.uv[i1 * 2] + w2 * mesh.uv[i2 * 2];
						float v = w0 * mesh.uv[i0 * 2 + 1] + w1 * mesh.uv[i1 * 2 + 1] + w2 * mesh.uv[i2 * 2 + 1];
						u -= std::floor(u); v -= std::floor(v);
						int tx = std::min(tex->width - 1, (int)(u * tex->width));
						int ty = std::min(tex->height - 1, (int)(v * tex->height));
						const uint8_t* s = &tex->rgba[((size_t)ty * tex->width + tx) * 4];
						rgb[0] = s[0]; rgb[1] = s[1]; rgb[2] = s[2];
					}
					if (!mesh.colors.empty())
						for (int k = 0; k < 3; ++k)
						{
							const float factor = w0*mesh.colors[i0*3+k] + w1*mesh.colors[i1*3+k] + w2*mesh.colors[i2*3+k];
							const float srgb = rgb[k]/255.0f;
							const float linear = (srgb <= 0.04045f ? srgb/12.92f : std::pow((srgb+0.055f)/1.055f, 2.4f)) * factor;
							rgb[k] = 255*(linear <= 0.0031308f ? 12.92f*linear : 1.055f*std::pow(linear, 1/2.4f)-0.055f);
						}
					zb = z;
					uint8_t* o = out + ((size_t)y * W + x) * 3;
					for (int k = 0; k < 3; k++) o[k] = (uint8_t)std::min(255.0f, rgb[k] * shade);
				}
		}
	}
	return img;
}

// ---------- panel ----------

cModelPreviewPanel::cModelPreviewPanel(wxWindow* parent, const wxPoint& pos, const wxSize& size)
	: wxPanel(parent, wxID_ANY, pos, size, wxBORDER_SUNKEN)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	message = "No model loaded";
	Bind(wxEVT_PAINT, &cModelPreviewPanel::onPaint, this);
	Bind(wxEVT_SIZE, &cModelPreviewPanel::onSize, this);
	Bind(wxEVT_LEFT_DOWN, &cModelPreviewPanel::onMouse, this);
	Bind(wxEVT_MOTION, &cModelPreviewPanel::onMouse, this);
	Bind(wxEVT_MOUSEWHEEL, &cModelPreviewPanel::onMouse, this);
	Bind(wxEVT_LEFT_DCLICK, &cModelPreviewPanel::onMouse, this);
}

void cModelPreviewPanel::showModel(const std::string& ydrPath, const std::vector<std::string>& ytdPaths)
{
	PreviewModel loaded;
	std::string err;
	if (!loadPreviewModel(ydrPath, ytdPaths, loaded, err))
	{
		clearModel("Preview unavailable: " + wxString(err));
		return;
	}
	showModel(std::move(loaded));
}

void cModelPreviewPanel::showModel(PreviewModel loaded)
{
	model = std::move(loaded);
	hasModel = true;
	float ex = model.bmax[0] - model.bmin[0], ey = model.bmax[1] - model.bmin[1];
	baseYaw = (ey > ex) ? 1.5707963f : 0.0f;
	yaw = 0.35f;
	pitch = 0.2f;
	zoom = 1.0f;
	message = wxString(model.info);
	render();
}

void cModelPreviewPanel::clearModel(const wxString& msg)
{
	hasModel = false;
	model = PreviewModel();
	message = msg;
	bitmap = wxBitmap();
	Refresh();
}

void cModelPreviewPanel::render()
{
	wxSize s = GetClientSize();
	if (!hasModel || s.x < 8 || s.y < 8) { Refresh(); return; }
	bitmap = wxBitmap(renderModel(model, s.x, s.y, baseYaw + yaw, pitch, zoom));
	Refresh();
}

void cModelPreviewPanel::onSize(wxSizeEvent& evt)
{
	render();
	evt.Skip();
}

void cModelPreviewPanel::onMouse(wxMouseEvent& evt)
{
	if (!hasModel) return;
	if (evt.GetEventType() == wxEVT_LEFT_DOWN) { lastMouse = evt.GetPosition(); SetFocus(); }
	else if (evt.GetEventType() == wxEVT_LEFT_DCLICK) { yaw = 0.35f; pitch = 0.2f; zoom = 1.0f; render(); }
	else if (evt.GetEventType() == wxEVT_MOUSEWHEEL)
	{
		zoom *= evt.GetWheelRotation() > 0 ? 1.1f : 1.0f / 1.1f;
		zoom = std::max(0.2f, std::min(zoom, 8.0f));
		render();
	}
	else if (evt.Dragging() && evt.LeftIsDown())
	{
		wxPoint p = evt.GetPosition();
		yaw += (p.x - lastMouse.x) * 0.01f;
		pitch = std::max(-1.5f, std::min(1.5f, pitch + (p.y - lastMouse.y) * 0.01f));
		lastMouse = p;
		render();
	}
}

void cModelPreviewPanel::onPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(wxColour(55, 55, 62)));
	dc.Clear();
	if (hasModel && bitmap.IsOk()) dc.DrawBitmap(bitmap, 0, 0);
	dc.SetTextForeground(wxColour(230, 230, 230));
	wxSize s = GetClientSize();
	if (hasModel) dc.DrawText(message, 6, s.y - 20);
	else
	{
		wxSize ts = dc.GetTextExtent(message);
		dc.DrawText(message, std::max(4, (s.x - ts.x) / 2), s.y / 2 - ts.y / 2);
	}
}
