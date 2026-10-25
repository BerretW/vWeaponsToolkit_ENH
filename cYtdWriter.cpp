// SPDX-License-Identifier: GPL-3.0-or-later
// Original serializer for the gen8/gen9 texture layouts and BC1/BC3 encoder.
// Format research references are listed in THIRD_PARTY_NOTICES.txt.
#include "cYtdWriter.h"
#include "cRscWriter.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace
{
	using Pixel = std::array<uint8_t, 4>;

	std::string normalizeName(const std::string& name)
	{
		std::string result = normalizeAssetName(name, "Texture");
		if (result.compare(0, 10, "script_rt_") == 0)
			throw std::invalid_argument("script_rt_ textures require an uncompressed runtime-texture export, not BC1/BC3.");
		return result;
	}

	uint16_t pack565(const Pixel& c)
	{
		return static_cast<uint16_t>(((c[0] * 31 + 127) / 255) << 11 |
			((c[1] * 63 + 127) / 255) << 5 | (c[2] * 31 + 127) / 255);
	}

	Pixel unpack565(uint16_t c)
	{
		const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
		return { static_cast<uint8_t>((r << 3) | (r >> 2)),
			static_cast<uint8_t>((g << 2) | (g >> 4)), static_cast<uint8_t>((b << 3) | (b >> 2)), 255 };
	}

	void append16(std::vector<uint8_t>& out, uint16_t value)
	{
		out.push_back(static_cast<uint8_t>(value));
		out.push_back(static_cast<uint8_t>(value >> 8));
	}

	void encodeColor(const std::array<Pixel, 16>& block, std::vector<uint8_t>& out)
	{
		Pixel lo = { 255, 255, 255, 255 }, hi = { 0, 0, 0, 255 };
		for (const auto& p : block)
			for (unsigned c = 0; c < 3; ++c)
			{
				lo[c] = std::min(lo[c], p[c]);
				hi[c] = std::max(hi[c], p[c]);
			}
		uint16_t a = pack565(hi), b = pack565(lo);
		uint32_t indices = 0;
		for (unsigned iteration = 0; iteration < 3; ++iteration)
		{
			if (a < b) std::swap(a, b);
			if (a == b) { if (a < 65535) ++a; else --b; }
			Pixel palette[4] = { unpack565(a), unpack565(b), {}, {} };
			for (unsigned c = 0; c < 3; ++c)
			{
				palette[2][c] = static_cast<uint8_t>((2 * palette[0][c] + palette[1][c]) / 3);
				palette[3][c] = static_cast<uint8_t>((palette[0][c] + 2 * palette[1][c]) / 3);
			}
			indices = 0;
			float aa = 0, ab = 0, bb = 0, ap[3] = {}, bp[3] = {};
			const float weight[4] = { 1, 0, 2.0f / 3, 1.0f / 3 };
			for (unsigned i = 0; i < 16; ++i)
			{
				unsigned best = 0, error = std::numeric_limits<unsigned>::max();
				for (unsigned k = 0; k < 4; ++k)
				{
					unsigned distance = 0;
					for (unsigned c = 0; c < 3; ++c)
					{
						const int delta = int(block[i][c]) - palette[k][c];
						distance += delta * delta;
					}
					if (distance < error) { error = distance; best = k; }
				}
				indices |= best << (2 * i);
				const float u = weight[best], v = 1 - u;
				aa += u * u; ab += u * v; bb += v * v;
				for (unsigned c = 0; c < 3; ++c)
				{
					ap[c] += u * block[i][c];
					bp[c] += v * block[i][c];
				}
			}
			const float determinant = aa * bb - ab * ab;
			if (iteration == 2 || determinant < 0.0001f) break;
			for (unsigned c = 0; c < 3; ++c)
			{
				hi[c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, (ap[c] * bb - bp[c] * ab) / determinant + 0.5f)));
				lo[c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, (bp[c] * aa - ap[c] * ab) / determinant + 0.5f)));
			}
			a = pack565(hi); b = pack565(lo);
		}
		append16(out, a); append16(out, b);
		for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(indices >> (8 * i)));
	}

	void encodeAlpha(const std::array<Pixel, 16>& block, std::vector<uint8_t>& out)
	{
		uint8_t lo = 255, hi = 0;
		for (const auto& p : block) { lo = std::min(lo, p[3]); hi = std::max(hi, p[3]); }
		uint8_t palette[8] = { hi, lo, 0, 0, 0, 0, 0, 0 };
		if (hi > lo)
			for (unsigned i = 1; i < 7; ++i) palette[i + 1] = static_cast<uint8_t>(((7 - i) * hi + i * lo) / 7);
		else
		{
			for (unsigned i = 1; i < 5; ++i) palette[i + 1] = static_cast<uint8_t>(((5 - i) * hi + i * lo) / 5);
			palette[7] = 255;
		}
		uint64_t indices = 0;
		for (unsigned i = 0; i < 16; ++i)
		{
			int error = 256;
			unsigned best = 0;
			for (unsigned k = 0; k < 8; ++k)
			{
				int distance = std::abs(int(block[i][3]) - palette[k]);
				if (distance < error) { error = distance; best = k; }
			}
			indices |= uint64_t(best) << (3 * i);
		}
		out.push_back(hi); out.push_back(lo);
		for (unsigned i = 0; i < 6; ++i) out.push_back(static_cast<uint8_t>(indices >> (8 * i)));
	}

	std::vector<uint8_t> downsample(const std::vector<uint8_t>& src, uint32_t w, uint32_t h,
		YtdImageSemantic semantic)
	{
		const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
		std::vector<uint8_t> result(size_t(nw) * nh * 4);
		static const std::array<float, 256> linear = [] {
			std::array<float, 256> table = {};
			for (unsigned i = 0; i < 256; ++i)
			{
				float s = i / 255.0f;
				table[i] = s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
			}
			return table;
		}();
		for (uint32_t y = 0; y < nh; ++y)
			for (uint32_t x = 0; x < nw; ++x)
			{
				float rgb[3] = {}, alpha = 0;
				for (uint32_t dy = 0; dy < 2; ++dy)
					for (uint32_t dx = 0; dx < 2; ++dx)
					{
						const size_t p = (size_t(std::min(h - 1, y * 2 + dy)) * w + std::min(w - 1, x * 2 + dx)) * 4;
						alpha += src[p + 3];
						for (unsigned c = 0; c < 3; ++c)
							rgb[c] += semantic == YtdImageSemantic::ColorSrgb ?
								linear[src[p + c]] * src[p + 3] :
								semantic == YtdImageSemantic::TangentNormal ?
								src[p + c] / 127.5f - 1.0f : float(src[p + c]);
					}
				const size_t p = (size_t(y) * nw + x) * 4;
				result[p + 3] = static_cast<uint8_t>(alpha / 4 + 0.5f);
				if (semantic == YtdImageSemantic::TangentNormal)
				{
					const float length = std::sqrt(rgb[0]*rgb[0] + rgb[1]*rgb[1] + rgb[2]*rgb[2]);
					if (length > 0.000001f)
						for (float& c : rgb) c /= length;
					else { rgb[0] = 0; rgb[1] = 0; rgb[2] = 1; }
				}
				for (unsigned c = 0; c < 3; ++c)
				{
					float value;
					if (semantic == YtdImageSemantic::ColorSrgb)
					{
						const float v = alpha > 0 ? rgb[c] / alpha : 0;
						const float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f;
						value = s * 255;
					}
					else value = semantic == YtdImageSemantic::TangentNormal ?
						(rgb[c] + 1) * 127.5f : rgb[c] / 4;
					result[p + c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, value + 0.5f)));
				}
			}
		return result;
	}

	struct EncodedTexture
	{
		std::string name;
		uint32_t hash;
		uint32_t width, height;
		bool alpha;
		uint8_t levels = 0;
		uint32_t blocks = 0;
		std::vector<uint8_t> data;
	};

	EncodedTexture encode(const YtdImage& image, const std::string& name, bool gen9)
	{
		EncodedTexture t;
		t.name = name; t.hash = assetNameHash(name); t.width = image.width; t.height = image.height;
		t.alpha = false;
		for (size_t i = 3; i < image.rgba.size(); i += 4)
			if (image.rgba[i] != 255) { t.alpha = true; break; }
		auto pixels = image.rgba;
		uint32_t w = image.width, h = image.height;
		for (;;)
		{
			++t.levels;
			const uint32_t bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4);
			t.blocks += bw * bh;
			for (uint32_t by = 0; by < bh; ++by)
				for (uint32_t bx = 0; bx < bw; ++bx)
				{
					std::array<Pixel, 16> block;
					for (uint32_t i = 0; i < 16; ++i)
					{
						const size_t p = (size_t(std::min(h - 1, by * 4 + i / 4)) * w + std::min(w - 1, bx * 4 + i % 4)) * 4;
						std::copy_n(pixels.data() + p, 4, block[i].begin());
					}
					if (t.alpha) encodeAlpha(block, t.data);
					encodeColor(block, t.data);
				}
			if (gen9 ? (w == 1 && h == 1) : (w == 4 || h == 4)) break;
			pixels = downsample(pixels, w, h, image.semantic);
			w = std::max(1u, w / 2); h = std::max(1u, h / 2);
		}
		return t;
	}
}

YtdImage downsampleYtdImage(const YtdImage& image)
{
	if (!image.width || !image.height || image.width > 4096 || image.height > 4096 ||
		image.rgba.size() != size_t(image.width) * image.height * 4 ||
		(image.width & (image.width - 1)) || (image.height & (image.height - 1)))
		throw std::invalid_argument("Mip input must be RGBA with power-of-two dimensions, 1..4096.");
	if (image.semantic != YtdImageSemantic::ColorSrgb && image.semantic != YtdImageSemantic::LinearData &&
		image.semantic != YtdImageSemantic::TangentNormal)
		throw std::invalid_argument("Unsupported texture semantic.");
	return { image.name, std::max(1u, image.width / 2), std::max(1u, image.height / 2),
		downsample(image.rgba, image.width, image.height, image.semantic), image.semantic };
}

std::vector<uint8_t> buildYtd(const std::vector<YtdImage>& images, GameGeneration generation)
{
	if (generation != GameGeneration::Legacy && generation != GameGeneration::Enhanced)
		throw std::invalid_argument("Unsupported game generation.");
	if (images.empty() || images.size() > 1024)
		throw std::invalid_argument("A texture dictionary must contain 1..1024 textures.");
	const bool gen9 = generation == GameGeneration::Enhanced;
	std::vector<std::string> names;
	std::set<uint32_t> hashes;
	size_t inputBytes = 0;
	for (const auto& image : images)
	{
		if (image.semantic != YtdImageSemantic::ColorSrgb && image.semantic != YtdImageSemantic::LinearData &&
			image.semantic != YtdImageSemantic::TangentNormal)
			throw std::invalid_argument("Unsupported texture semantic.");
		const auto w = image.width, h = image.height;
		if (!w || !h || w > 4096 || h > 4096 || (w & (w - 1)) || (h & (h - 1)))
			throw std::invalid_argument("Texture '" + image.name + "': dimensions must be powers of two, 1..4096.");
		if (!gen9 && (w < 4 || h < 4))
			throw std::invalid_argument("Texture '" + image.name + "': Legacy BC1/BC3 dimensions must be at least 4x4.");
		if (image.rgba.size() != size_t(w) * h * 4)
			throw std::invalid_argument("Texture '" + image.name + "': RGBA byte count does not match its dimensions.");
		inputBytes += image.rgba.size();
		if (inputBytes > 128 * 1024 * 1024)
			throw std::length_error("Texture input exceeds the 128 MiB RGBA limit per dictionary.");
		auto name = normalizeName(image.name);
		if (!hashes.insert(assetNameHash(name)).second)
			throw std::invalid_argument("Duplicate texture name or hash collision: " + name);
		names.push_back(std::move(name));
	}
	std::vector<EncodedTexture> textures;
	for (size_t i = 0; i < images.size(); ++i) textures.push_back(encode(images[i], names[i], gen9));
	std::sort(textures.begin(), textures.end(), [](const EncodedTexture& a, const EncodedTexture& b) { return a.hash < b.hash; });

	cRscWriter writer;
	const auto root = writer.allocate(ResourceSegment::System, 64);
	writer.u32(root + 4, 1);
	writer.u32(root + 0x18, 1);
	const auto count = static_cast<uint16_t>(textures.size());
	const auto hashArray = writer.allocate(ResourceSegment::System, size_t(count) * 4);
	const auto textureArray = writer.allocate(ResourceSegment::System, size_t(count) * 8);
	writer.u64(root + 0x20, hashArray);
	writer.u16(root + 0x28, count); writer.u16(root + 0x2A, count);
	writer.u64(root + 0x30, textureArray);
	writer.u16(root + 0x38, count); writer.u16(root + 0x3A, count);
	for (size_t i = 0; i < textures.size(); ++i)
	{
		const auto& t = textures[i];
		const auto texture = writer.allocate(ResourceSegment::System, gen9 ? 128 : 144);
		const auto name = writer.allocate(ResourceSegment::System, t.name.size() + 1);
		const auto data = writer.allocate(ResourceSegment::Graphics, t.data.size());
		writer.bytes(name, t.name.c_str(), t.name.size() + 1);
		writer.bytes(data, t.data.data(), t.data.size());
		writer.u32(hashArray + i * 4, t.hash);
		writer.u64(textureArray + i * 8, texture);
		writer.u32(texture + 4, 1);
		writer.u64(texture + 0x28, name);
		const uint32_t blockStride = t.alpha ? 16 : 8;
		if (gen9)
		{
			writer.u32(texture + 8, t.blocks);
			writer.u32(texture + 0xC, blockStride);
			writer.u32(texture + 0x10, 0x00260208);
			writer.u16(texture + 0x18, static_cast<uint16_t>(t.width));
			writer.u16(texture + 0x1A, static_cast<uint16_t>(t.height));
			writer.u16(texture + 0x1C, 1);
			writer.u8(texture + 0x1E, 1); // TextureDimensionG9.Texture2D
			writer.u8(texture + 0x1F, t.alpha ? 77 : 71);
			writer.u8(texture + 0x20, 255);
			writer.u8(texture + 0x22, t.levels);
			writer.u8(texture + 0x23, 0x28);
			writer.u16(texture + 0x26, 1);
			writer.u64(texture + 0x30, texture + 0x58);
			writer.u64(texture + 0x38, data);
			writer.u32(texture + 0x40, 20);
			writer.u32(texture + 0x44, 2);
			writer.u64(texture + 0x58, 0x00000001406b77d8);
			writer.u16(texture + 0x68, 0x41);
			writer.u16(texture + 0x6A, 0xFFFF);
			writer.u32(texture + 0x6C, 0xFFFFFFFF);
		}
		else
		{
			writer.u16(texture + 0x30, 1);
			writer.u32(texture + 0x40, 20);
			writer.u16(texture + 0x50, static_cast<uint16_t>(t.width));
			writer.u16(texture + 0x52, static_cast<uint16_t>(t.height));
			writer.u16(texture + 0x54, 1);
			// Legacy stores compressed bytes per texel row, not per block row.
			writer.u16(texture + 0x56, static_cast<uint16_t>((t.width / 4) * blockStride / 4));
			writer.u32(texture + 0x58, t.alpha ? 0x35545844 : 0x31545844);
			writer.u8(texture + 0x5D, t.levels);
			writer.u64(texture + 0x70, data);
		}
	}
	return writer.build(gen9 ? 5 : 13);
}
