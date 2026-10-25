// SPDX-License-Identifier: GPL-3.0-or-later
// Original gen8/gen9 serializer. Shader binding facts were checked against local fixtures.
#include "cYdrWriter.h"
#include "cRscWriter.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
	const uint16_t vertexStride = 52;
	const unsigned copies = 12;
	using Vec3 = std::array<float, 3>;
	using Vec4 = std::array<float, 4>;

	struct Bounds
	{
		Vec3 min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
		Vec3 max = { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
		void add(const Vec3& p)
		{
			for (unsigned k = 0; k < 3; ++k) { min[k] = std::min(min[k], p[k]); max[k] = std::max(max[k], p[k]); }
		}
	};

	size_t align16(size_t value) { return (value + 15) & ~size_t(15); }

	void finite(float value, const char* field)
	{
		if (!std::isfinite(value) || std::fabs(value) > 1000000)
			throw std::invalid_argument(std::string("Invalid finite/range value for ") + field + ".");
	}

	void unit(const Vec3& v, const char* field)
	{
		double length = 0;
		for (float x : v) { finite(x, field); length += double(x) * x; }
		if (std::fabs(length - 1) > 0.002)
			throw std::invalid_argument(std::string(field) + " must be a unit vector.");
	}

	std::vector<Bounds> validate(const YdrDrawable& d, GameGeneration generation)
	{
		if (generation != GameGeneration::Legacy && generation != GameGeneration::Enhanced)
			throw std::invalid_argument("Unsupported game generation.");
		normalizeAssetName(d.name, "Drawable");
		if (d.materials.empty() || d.materials.size() > 128 || d.meshes.empty() || d.meshes.size() > 128)
			throw std::invalid_argument("A drawable requires 1..128 materials and 1..128 meshes.");
		finite(d.lodDistance, "LOD distance");
		if (d.lodDistance <= 0) throw std::invalid_argument("LOD distance must be positive.");
		for (const auto& m : d.materials)
		{
			for (const auto& name : { m.diffuseTexture, m.normalTexture, m.specularTexture })
			{
				auto normalized = normalizeAssetName(name, "Texture");
				if (normalized.compare(0, 10, "script_rt_") == 0)
					throw std::invalid_argument("Runtime script_rt_ textures are not supported by this material profile.");
			}
			for (float value : { m.bumpiness, m.wetness, m.specularIntensity, m.specularFalloff, m.specularFresnel })
			{
				finite(value, "material parameter");
				if (value < 0) throw std::invalid_argument("Material parameters must be nonnegative.");
			}
		}
		std::vector<Bounds> bounds;
		size_t vertexBytes = 0, indexBytes = 0;
		for (const auto& mesh : d.meshes)
		{
			if (mesh.material >= d.materials.size()) throw std::invalid_argument("Mesh material index is out of range.");
			if (mesh.vertices.size() < 3 || mesh.vertices.size() > 65535)
				throw std::invalid_argument("Each mesh requires 3..65535 vertices; split larger meshes before writing.");
			if (mesh.indices.empty() || mesh.indices.size() % 3 || mesh.indices.size() > 3000000)
				throw std::invalid_argument("Indices must contain complete triangles, at most 3000000 indices per mesh.");
			vertexBytes += mesh.vertices.size() * vertexStride;
			indexBytes += mesh.indices.size() * 2;
			if (vertexBytes + indexBytes > 120 * 1024 * 1024)
				throw std::length_error("Drawable vertex/index data exceeds the 120 MiB limit.");
			Bounds box;
			for (const auto& v : mesh.vertices)
			{
				for (float x : v.position) finite(x, "position");
				for (float x : v.uv) finite(x, "UV");
				unit(v.normal, "Normal");
				unit({ v.tangent[0], v.tangent[1], v.tangent[2] }, "Tangent");
				const float dot = v.normal[0] * v.tangent[0] + v.normal[1] * v.tangent[1] + v.normal[2] * v.tangent[2];
				if (std::fabs(dot) > 0.002f || (v.tangent[3] != 1 && v.tangent[3] != -1))
					throw std::invalid_argument("Tangent must be perpendicular to the normal and have handedness +1 or -1.");
				box.add(v.position);
			}
			for (size_t i = 0; i < mesh.indices.size(); i += 3)
			{
				uint32_t ids[3] = { mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2] };
				for (auto id : ids)
					if (id >= mesh.vertices.size()) throw std::invalid_argument("Triangle index is out of range.");
				const auto& a = mesh.vertices[ids[0]].position;
				const auto& b = mesh.vertices[ids[1]].position;
				const auto& c = mesh.vertices[ids[2]].position;
				const double ux = double(b[0]) - a[0], uy = double(b[1]) - a[1], uz = double(b[2]) - a[2];
				const double vx = double(c[0]) - a[0], vy = double(c[1]) - a[1], vz = double(c[2]) - a[2];
				const double x = uy * vz - uz * vy, y = uz * vx - ux * vz, z = ux * vy - uy * vx;
				if (x * x + y * y + z * z == 0)
					throw std::invalid_argument("Degenerate triangle; repair the source mesh before writing.");
			}
			bounds.push_back(box);
		}
		return bounds;
	}

	void vec3(cRscWriter& w, uint64_t at, const Vec3& value)
	{
		for (unsigned k = 0; k < 3; ++k) w.f32(at + 4 * k, value[k]);
	}
	void vec4(cRscWriter& w, uint64_t at, const Vec4& value)
	{
		for (unsigned k = 0; k < 4; ++k) w.f32(at + 4 * k, value[k]);
	}
	void aabb(cRscWriter& w, uint64_t at, const Bounds& box)
	{
		vec3(w, at, box.min); vec3(w, at + 16, box.max);
	}
	uint64_t string(cRscWriter& w, const std::string& text)
	{
		const auto p = w.allocate(ResourceSegment::System, text.size() + 1);
		w.bytes(p, text.c_str(), text.size() + 1);
		return p;
	}
	void header(cRscWriter& w, uint64_t at) { w.u32(at + 4, 1); }

	uint64_t textureReference(cRscWriter& w, const std::string& name, bool gen9)
	{
		const auto t = w.allocate(ResourceSegment::System, 80);
		header(w, t);
		w.u64(t + 0x28, string(w, normalizeAssetName(name, "Texture")));
		if (gen9)
		{
			w.u32(t + 0x10, 0x00260208);
			w.u8(t + 0x1E, 1);
			w.u8(t + 0x20, 255);
			w.u16(t + 0x26, 1);
		}
		else w.u16(t + 0x30, 1);
		return t;
	}

	void shader(cRscWriter& w, uint64_t at, const YdrMaterial& material, bool gen9)
	{
		const uint64_t textures[3] = { textureReference(w, material.diffuseTexture, gen9),
			textureReference(w, material.normalTexture, gen9), textureReference(w, material.specularTexture, gen9) };
		const uint32_t shaderHash = assetNameHash("normal_spec_decal");
		if (!gen9)
		{
			const char* names[12] = { "diffusesampler", "bumpsampler", "specsampler", "usetessellation",
				"wetnessmultiplier", "bumpiness", "specmapintmask", "specularintensitymult",
				"specularfalloffmult", "specularfresnel", "globalanimuv1", "globalanimuv0" };
			const Vec4 values[9] = { {0, 0, 0, 0}, {material.wetness, 0, 0, 0}, {material.bumpiness, 0, 0, 0},
				{1, 0, 0, 0}, {material.specularIntensity, 0, 0, 0}, {material.specularFalloff, 0, 0, 0},
				{material.specularFresnel, 0, 0, 0}, {0, 1, 0, 0}, {1, 0, 0, 0} };
			const auto p = w.allocate(ResourceSegment::System, 2080);
			for (unsigned i = 0; i < 12; ++i)
			{
				w.u8(p + i * 16, i < 3 ? 0 : 1);
				w.u64(p + i * 16 + 8, i < 3 ? textures[i] : p + 192 + (i - 3) * 16);
				w.u32(p + 336 + i * 4, assetNameHash(names[i]));
				if (i >= 3) vec4(w, p + 192 + (i - 3) * 16, values[i - 3]);
			}
			w.u64(at, p);
			w.u32(at + 8, shaderHash);
			w.u8(at + 0x10, 12);
			w.u8(at + 0x11, 2);
			w.u16(at + 0x12, 0x8000);
			w.u16(at + 0x14, 336);
			w.u16(at + 0x16, 416);
			w.u32(at + 0x18, assetNameHash("normal_spec_decal.sps"));
			w.u32(at + 0x20, 0xFF04);
			w.u8(at + 0x27, 3);
			return;
		}

		// Two constant buffers (32/48 bytes) and four resource slots, replicated for 12 contexts.
		constexpr unsigned pointerBytes = 2 * 8 * copies;
		constexpr unsigned bufferBytes = (32 + 48) * copies;
		constexpr unsigned textureBytes = 4 * 8 * copies;
		constexpr unsigned dataSize = pointerBytes + bufferBytes + textureBytes + 5;
		const auto p = w.allocate(ResourceSegment::System, dataSize);
		const auto refs = p + pointerBytes + bufferBytes;
		for (unsigned copy = 0; copy < copies; ++copy)
		{
			const auto cb0 = p + pointerBytes + copy * 80, cb1 = cb0 + 32;
			w.u64(p + copy * 16, cb0); w.u64(p + copy * 16 + 8, cb1);
			vec4(w, cb0, { 1, 0, 0, 0 }); vec4(w, cb0 + 16, { 0, 1, 0, 0 });
			w.f32(cb1, material.specularFresnel);
			w.f32(cb1 + 4, material.specularFalloff);
			w.f32(cb1 + 8, material.specularIntensity);
			vec4(w, cb1 + 16, { 1, 0, 0, material.bumpiness });
			w.f32(cb1 + 32, material.wetness);
			w.f32(cb1 + 40, 1);
		}
		// Only the first resource set is serialized; the remaining sets are runtime scratch.
		w.u64(refs, textures[0]); w.u64(refs + 16, textures[1]); w.u64(refs + 24, textures[2]);
		for (unsigned i = 0; i < 5; ++i) w.u8(refs + textureBytes + i, static_cast<uint8_t>(i));
		const uint32_t hashes[19] = { 0xc8e8c282, 0x8e84df1f, 0x1e77bf4c, 0xc6f46e79,
			0x184d4d47, 0xe44690bb, 0xf1fe2b71, 0x24c5ab07, 0x49c32b64,
			0xd79bfc1e, 0xba54c190, 0xb5fe3acb, 0xa1f60489, 0xa98e2243,
			0x92db11fb, 0xf6712b81, 0x32dd9ff5, 0x4620a35d, 0xe9437406 };
		const uint32_t descriptors[19] = { 0, 4, 8, 12, 2, 6, 10, 14, 18,
			0x00c00003, 0x00c01003, 0x00400007, 0x00400407, 0x00400807,
			0x00c01007, 0x00401c07, 0x00402007, 0x00402407, 0x00402807 };
		const auto infos = w.allocate(ResourceSegment::System, 8 + 19 * 8);
		const uint8_t infoHeader[8] = { 2, 4, 0, 5, 19, 0, 0, copies };
		w.bytes(infos, infoHeader, sizeof(infoHeader));
		for (unsigned i = 0; i < 19; ++i)
		{
			w.u32(infos + 8 + i * 8, hashes[i]);
			w.u32(infos + 12 + i * 8, descriptors[i]);
		}
		w.u32(at, shaderHash); w.u32(at + 4, 0x6D657461);
		w.u64(at + 8, p); w.u64(at + 16, refs); w.u64(at + 32, infos);
		w.u8(at + 57, 2); w.u16(at + 58, dataSize); w.u32(at + 60, 0xFF04);
	}

	uint64_t srv(cRscWriter& w)
	{
		const auto p = w.allocate(ResourceSegment::System, 32);
		w.u64(p, 0x1406b77d8);
		w.u16(p + 16, 0x14); w.u16(p + 18, 0xFFFF); w.u32(p + 20, 0xFFFFFFFF);
		return p;
	}

	void geometry(cRscWriter& w, uint64_t at, const YdrMesh& mesh, bool gen9)
	{
		const auto vb = w.allocate(ResourceSegment::System, gen9 ? 64 : 128);
		const auto ib = w.allocate(ResourceSegment::System, gen9 ? 64 : 96);
		const auto decl = w.allocate(ResourceSegment::System, gen9 ? 320 : 16);
		const auto vertices = w.allocate(ResourceSegment::System, mesh.vertices.size() * vertexStride);
		const auto indices = w.allocate(ResourceSegment::System, mesh.indices.size() * 2);
		for (size_t i = 0; i < mesh.vertices.size(); ++i)
		{
			const auto p = vertices + i * vertexStride;
			const auto& v = mesh.vertices[i];
			vec3(w, p, v.position); vec3(w, p + 12, v.normal);
			w.bytes(p + 24, v.color.data(), 4);
			w.f32(p + 28, v.uv[0]); w.f32(p + 32, v.uv[1]);
			vec4(w, p + 36, v.tangent);
		}
		for (size_t i = 0; i < mesh.indices.size(); ++i) w.u16(indices + i * 2, static_cast<uint16_t>(mesh.indices[i]));
		header(w, vb); header(w, ib); header(w, at);
		w.u64(at + 0x18, vb); w.u64(at + 0x38, ib);
		w.u32(at + 0x58, static_cast<uint32_t>(mesh.indices.size()));
		w.u32(at + 0x5C, static_cast<uint32_t>(mesh.indices.size() / 3));
		w.u16(at + 0x60, static_cast<uint16_t>(mesh.vertices.size()));
		w.u16(at + 0x62, 3); w.u16(at + 0x70, vertexStride);
		w.u64(at + 0x78, vertices);
		w.u32(ib + 8, static_cast<uint32_t>(mesh.indices.size()));
		if (gen9)
		{
			w.u32(vb + 8, static_cast<uint32_t>(mesh.vertices.size())); w.u16(vb + 12, vertexStride);
			w.u32(vb + 16, 0x00580409); w.u64(vb + 24, vertices);
			w.u64(vb + 48, srv(w)); w.u64(vb + 56, decl);
			w.u16(ib + 12, 2); w.u32(ib + 16, 0x0058020A); w.u64(ib + 24, indices);
			w.u64(ib + 48, srv(w));
			const unsigned slots[5] = { 0, 4, 8, 24, 28 }, offsets[5] = { 0, 12, 36, 24, 28 };
			const uint8_t sizes[5] = { 12, 12, 16, 4, 8 }, types[5] = { 6, 6, 2, 28, 16 };
			for (unsigned i = 0; i < 5; ++i)
			{
				w.u32(decl + slots[i] * 4, offsets[i]);
				w.u8(decl + 208 + slots[i], sizes[i]); w.u8(decl + 260 + slots[i], types[i]);
			}
			w.u64(decl + 312, (uint64_t(mesh.vertices.size()) << 10) | (uint64_t(vertexStride) << 2));
		}
		else
		{
			w.u16(vb + 8, vertexStride); w.u64(vb + 16, vertices);
			w.u32(vb + 24, static_cast<uint32_t>(mesh.vertices.size()));
			w.u64(vb + 32, vertices); w.u64(vb + 48, decl); w.u64(ib + 16, indices);
			w.u32(decl, 0x4059); w.u16(decl + 4, vertexStride); w.u8(decl + 7, 5);
			w.u64(decl + 8, 6ull | (6ull << 12) | (9ull << 16) | (5ull << 24) | (7ull << 56));
		}
	}
}

std::vector<uint8_t> buildYdr(const YdrDrawable& drawable, GameGeneration generation)
{
	const auto meshBounds = validate(drawable, generation);
	const bool gen9 = generation == GameGeneration::Enhanced;
	Bounds bounds;
	for (const auto& box : meshBounds) { bounds.add(box.min); bounds.add(box.max); }
	cRscWriter w;
	const auto root = w.allocate(ResourceSegment::System, 208);
	header(w, root);
	w.u64(root + 0xA8, string(w, normalizeAssetName(drawable.name, "Drawable") + ".#dr"));
	const auto group = w.allocate(ResourceSegment::System, 64);
	const auto shaders = w.allocate(ResourceSegment::System, drawable.materials.size() * 8);
	header(w, group); w.u64(root + 16, group); w.u64(group + 16, shaders);
	w.u16(group + 24, static_cast<uint16_t>(drawable.materials.size()));
	w.u16(group + 26, static_cast<uint16_t>(drawable.materials.size()));
	w.u32(group + 48, gen9 ? 0 : 4);
	for (size_t i = 0; i < drawable.materials.size(); ++i)
	{
		const auto s = w.allocate(ResourceSegment::System, gen9 ? 64 : 48);
		w.u64(shaders + i * 8, s);
		shader(w, s, drawable.materials[i], gen9);
	}
	const auto n = drawable.meshes.size();
	const size_t mapOffset = 48;
	const size_t pointerOffset = n == 1 ? 56 : align16(mapOffset + n * 2);
	const size_t boundsOffset = align16(pointerOffset + n * 8);
	const size_t geometryOffset = boundsOffset + (n == 1 ? 1 : n + 1) * 32;
	const size_t modelSize = geometryOffset + (n - 1) * 160 + 152;
	const size_t modelsSize = align16(32 + modelSize);
	const auto lod = w.allocate(ResourceSegment::System, modelsSize);
	const auto model = lod + 32;
	header(w, model);
	w.u64(lod, lod + 16); w.u16(lod + 8, 1); w.u16(lod + 10, 1); w.u64(lod + 16, model);
	w.u64(model + 8, model + pointerOffset);
	w.u16(model + 16, static_cast<uint16_t>(n)); w.u16(model + 18, static_cast<uint16_t>(n));
	w.u64(model + 24, model + boundsOffset); w.u64(model + 32, model + mapOffset);
	w.u16(model + 44, 0xFF); w.u16(model + 46, static_cast<uint16_t>(n));
	if (n > 1) aabb(w, model + boundsOffset, bounds);
	for (size_t i = 0; i < n; ++i)
	{
		const auto g = model + geometryOffset + i * 160;
		w.u16(model + mapOffset + i * 2, drawable.meshes[i].material);
		w.u64(model + pointerOffset + i * 8, g);
		aabb(w, model + boundsOffset + (n == 1 ? 0 : i + 1) * 32, meshBounds[i]);
		geometry(w, g, drawable.meshes[i], gen9);
	}
	Vec3 center;
	double radiusSquared = 0;
	for (unsigned k = 0; k < 3; ++k)
	{
		center[k] = (bounds.min[k] + bounds.max[k]) * 0.5f;
		double extent = std::max(double(bounds.max[k]) - center[k], double(center[k]) - bounds.min[k]);
		radiusSquared += extent * extent;
	}
	vec3(w, root + 32, center);
	w.f32(root + 44, std::nextafter(static_cast<float>(std::sqrt(radiusSquared)), std::numeric_limits<float>::infinity()));
	vec3(w, root + 48, bounds.min); vec3(w, root + 64, bounds.max);
	w.u32(root + 60, 0x7F800001); w.u32(root + 76, 0x7F800001);
	w.u64(root + 80, lod);
	for (unsigned i = 0; i < 4; ++i) w.f32(root + 112 + i * 4, drawable.lodDistance);
	w.u32(root + 128, 0xFF01);
	if (!gen9)
	{
		w.u16(root + 154, static_cast<uint16_t>(modelsSize / 16));
		w.u64(root + 160, lod);
	}
	return w.build(gen9 ? 159 : 165);
}
