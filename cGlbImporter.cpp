#include <wx/wx.h>
#include <wx/file.h>
#include <wx/mstream.h>
#include "cGlbImporter.h"
#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <set>

namespace
{
	constexpr size_t fileLimit = 64 * 1024 * 1024;
	constexpr size_t memoryLimit = 128 * 1024 * 1024;
	using V3 = std::array<float, 3>;
	using Matrix = std::array<float, 16>;

	struct alignas(std::max_align_t) Allocation { size_t size; };
	void* allocate(void* user, cgltf_size size)
	{
		auto& used = *static_cast<size_t*>(user);
		if (size > memoryLimit - used) return nullptr;
		auto* p = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
		if (!p) return nullptr;
		p->size = size; used += size;
		return p + 1;
	}
	void release(void* user, void* ptr)
	{
		if (!ptr) return;
		auto* p = static_cast<Allocation*>(ptr) - 1;
		*static_cast<size_t*>(user) -= p->size;
		std::free(p);
	}
	[[noreturn]] void fail(const std::string& text) { throw std::runtime_error("GLB: " + text); }
	void check(cgltf_result result, const char* operation)
	{
		if (result != cgltf_result_success)
			fail(std::string(operation) + " failed (cgltf error " + std::to_string(result) + ").");
	}
	void warning(GlbModel& model, const std::string& text)
	{
		if (std::find(model.warnings.begin(), model.warnings.end(), text) == model.warnings.end())
			model.warnings.push_back(text);
	}
	uint32_t u32(const uint8_t* p)
	{
		return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
	}
	float finite(float value)
	{
		if (!std::isfinite(value) || std::fabs(value) > 1000000) fail("Non-finite or excessive numeric value.");
		return value;
	}
	float dot(const V3& a, const V3& b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
	V3 cross(const V3& a, const V3& b)
	{
		return { a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0] };
	}
	V3 normalize(V3 value)
	{
		double length = 0;
		for (float x : value) { finite(x); length += double(x)*x; }
		if (length < 1e-24) fail("Zero-length normal/tangent or degenerate triangle.");
		for (float& x : value) x = static_cast<float>(x / std::sqrt(length));
		return value;
	}
	V3 zUp(const V3& v) { return { v[0], -v[2], v[1] }; }
	V3 direction(const Matrix& m, const V3& p)
	{
		V3 out;
		for (unsigned k = 0; k < 3; ++k) out[k] = finite(m[k]*p[0] + m[k+4]*p[1] + m[k+8]*p[2]);
		return out;
	}
	const cgltf_accessor* attribute(const cgltf_primitive& primitive, cgltf_attribute_type type, int index = 0)
	{
		const cgltf_accessor* result = nullptr;
		for (size_t i = 0; i < primitive.attributes_count; ++i)
			if (primitive.attributes[i].type == type && primitive.attributes[i].index == index)
			{
				if (result) fail("Duplicate vertex attribute.");
				result = primitive.attributes[i].data;
			}
		return result;
	}
	void accessor(const cgltf_accessor* a, cgltf_type type, size_t count)
	{
		if (!a || a->type != type || a->count != count || a->is_sparse || !a->buffer_view)
			fail("Missing/mismatched vertex attribute, sparse accessor or missing buffer view.");
	}
	template<size_t N> std::array<float, N> read(const cgltf_accessor* a, size_t index)
	{
		std::array<float, N> result = {};
		if (!cgltf_accessor_read_float(a, index, result.data(), N)) fail("Could not read vertex attribute.");
		for (float x : result) finite(x);
		return result;
	}

	PreviewTexture image(const cgltf_image& source, size_t& decodedBytes)
	{
		if (source.uri || !source.buffer_view) fail("Images must be embedded PNG/JPEG buffer views.");
		const auto* bytes = cgltf_buffer_view_data(source.buffer_view);
		const size_t count = source.buffer_view->size;
		if (!bytes) fail("Missing embedded image data.");
		uint32_t width = 0, height = 0;
		wxBitmapType type = wxBITMAP_TYPE_INVALID;
		const uint8_t signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
		if (count >= 24 && std::equal(signature, signature + 8, bytes))
		{
			auto be32 = [](const uint8_t* p) { return (uint32_t(p[0])<<24) | (uint32_t(p[1])<<16) | (uint32_t(p[2])<<8) | p[3]; };
			width = be32(bytes + 16); height = be32(bytes + 20); type = wxBITMAP_TYPE_PNG;
		}
		else if (count >= 4 && bytes[0] == 255 && bytes[1] == 216)
		{
			size_t at = 2;
			while (at + 4 <= count)
			{
				if (bytes[at++] != 255) fail("Invalid JPEG marker.");
				while (at < count && bytes[at] == 255) ++at;
				if (at >= count) break;
				const auto marker = bytes[at++];
				if (marker == 0xDA || marker == 0xD9) break;
				if (marker == 1 || (marker >= 0xD0 && marker <= 0xD8)) continue;
				if (at + 2 > count) break;
				const size_t length = size_t(bytes[at])*256 + bytes[at+1];
				if (length < 2 || length > count - at) fail("Invalid JPEG segment.");
				if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
				{
					if (length < 8) fail("Invalid JPEG dimensions.");
					height = uint32_t(bytes[at+3])*256 + bytes[at+4];
					width = uint32_t(bytes[at+5])*256 + bytes[at+6];
					type = wxBITMAP_TYPE_JPEG; break;
				}
				at += length;
			}
		}
		if (!width || !height || width > 4096 || height > 4096) fail("Embedded PNG/JPEG dimensions must be 1..4096.");
		const size_t size = size_t(width)*height*4;
		if (size > memoryLimit - decodedBytes) fail("Decoded images exceed 128 MiB.");
		wxMemoryInputStream stream(bytes, count);
		wxImage decoded;
		if (!decoded.LoadFile(stream, type) || decoded.GetWidth() != int(width) || decoded.GetHeight() != int(height))
			fail("Embedded image could not be decoded.");
		PreviewTexture result;
		result.width = int(width); result.height = int(height); result.rgba.resize(size);
		for (size_t i = 0; i < size/4; ++i)
		{
			std::copy_n(decoded.GetData()+i*3, 3, result.rgba.data()+i*4);
			const auto* rgb = decoded.GetData()+i*3;
			result.rgba[i*4+3] = decoded.HasAlpha() ? decoded.GetAlpha()[i] :
				(decoded.HasMask() && rgb[0] == decoded.GetMaskRed() &&
					rgb[1] == decoded.GetMaskGreen() && rgb[2] == decoded.GetMaskBlue() ? 0 : 255);
		}
		decodedBytes += size;
		return result;
	}
}

GlbModel importGlb(const std::vector<uint8_t>& bytes)
{
	if (bytes.size() < 20 || bytes.size() > fileLimit || u32(bytes.data()) != 0x46546C67 ||
		u32(bytes.data()+4) != 2 || u32(bytes.data()+8) != bytes.size())
		fail("Expected a GLB 2.0 file, at most 64 MiB, with a valid length.");
	if (u32(bytes.data()+12) > 8*1024*1024 || u32(bytes.data()+16) != 0x4E4F534A)
		fail("Expected a JSON chunk of at most 8 MiB.");
	size_t allocated = 0;
	cgltf_options options = {};
	options.memory = { allocate, release, &allocated };
	cgltf_data* raw = nullptr;
	check(cgltf_parse(&options, bytes.data(), bytes.size(), &raw), "Parsing");
	std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
	if (data->extensions_required_count) fail("Unsupported required extension: " + std::string(data->extensions_required[0]));
	if (data->buffers_count != 1 || data->buffers[0].uri) fail("Exactly one embedded binary buffer is required; external/data URIs are not loaded.");
	for (size_t i = 0; i < data->images_count; ++i)
		if (data->images[i].uri) fail("External/data image URIs are not supported; embed images in the GLB.");
	check(cgltf_load_buffers(&options, data.get(), nullptr), "Loading embedded buffer");
	check(cgltf_validate(data.get()), "Validation");
	if (!data->scenes_count || data->nodes_count > 4096 || data->materials_count > 128 || data->images_count > 128)
		fail("Requires a scene, at most 4096 nodes, 128 materials and 128 images.");
	GlbModel out;
	warning(out, "Static inspection only: no weapon rigging, GTA shader conversion or resource export.");
	warning(out, "Preview uses base color and vertex colors only; alpha, normal maps and PBR lighting are not rendered.");
	if (data->animations_count) warning(out, "Animations are not played; showing the authored static node transforms.");
	const auto* scene = data->scene ? data->scene : &data->scenes[0];
	if (!data->scene && data->scenes_count > 1) warning(out, "No default scene: showing the first scene only.");
	size_t decodedBytes = 0, geometryBytes = 0;
	std::vector<int> images(data->images_count, -1);
	for (size_t i = 0; i < data->materials_count; ++i)
	{
		const auto& source = data->materials[i];
		GlbMaterial material;
		material.name = source.name ? source.name : "material_" + std::to_string(i);
		for (unsigned k = 0; k < 4; ++k)
		{
			const float value = finite(source.pbr_metallic_roughness.base_color_factor[k]);
			if (value < 0 || value > 1) fail("Base color factor must be in 0..1.");
			material.baseColor[k] = value;
		}
		const auto& view = source.pbr_metallic_roughness.base_color_texture;
		if (view.texture)
		{
			if (view.texcoord != 0 || view.has_transform) fail("Base color currently requires TEXCOORD_0 without a texture transform.");
			const auto* sampler = view.texture->sampler;
			if (sampler && (sampler->wrap_s != cgltf_wrap_mode_repeat || sampler->wrap_t != cgltf_wrap_mode_repeat))
				fail("Only repeat texture wrapping is currently supported.");
			if (!view.texture->image) fail("Base color texture has no PNG/JPEG image.");
			const size_t index = size_t(view.texture->image - data->images);
			if (images[index] < 0)
			{
				images[index] = static_cast<int>(out.textures.size());
				out.textures.push_back(image(*view.texture->image, decodedBytes));
			}
			material.texture = images[index];
		}
		out.materials.push_back(std::move(material));
	}
	// A missing glTF material is the standard white metallic-roughness material.
	const auto defaultMaterial = static_cast<uint16_t>(out.materials.size());
	out.materials.push_back({ "default", {1, 1, 1, 1}, -1 });
	std::vector<const cgltf_node*> pending;
	for (size_t i = scene->nodes_count; i; --i) pending.push_back(scene->nodes[i-1]);
	std::set<const cgltf_node*> visited;
	while (!pending.empty())
	{
		const auto* node = pending.back(); pending.pop_back();
		if (!visited.insert(node).second) fail("Repeated/cyclic scene node.");
		if (node->skin || node->has_mesh_gpu_instancing) fail("Skinned meshes and GPU instancing are not supported yet; export a static GLB.");
		for (size_t i = node->children_count; i; --i) pending.push_back(node->children[i-1]);
		if (!node->mesh) continue;
		Matrix m;
		cgltf_node_transform_world(node, m.data());
		for (float x : m) finite(x);
		if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1) fail("Node transform is not affine.");
		const V3 a = {m[0], m[1], m[2]}, b = {m[4], m[5], m[6]}, c = {m[8], m[9], m[10]};
		const V3 ca = cross(b, c), cb = cross(c, a), cc = cross(a, b);
		const float determinant = dot(a, ca);
		if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-12f) fail("Singular or near-zero node scale.");
		for (size_t p = 0; p < node->mesh->primitives_count; ++p)
		{
			const auto& primitive = node->mesh->primitives[p];
			if (primitive.type != cgltf_primitive_type_triangles || primitive.targets_count || primitive.has_draco_mesh_compression)
				fail("Only triangle primitives without morph targets/Draco compression are supported.");
			for (size_t i = 0; i < primitive.attributes_count; ++i)
				if (primitive.attributes[i].type == cgltf_attribute_type_joints ||
					primitive.attributes[i].type == cgltf_attribute_type_weights)
					fail("Bone weights require the future rigged importer.");
			const auto* pos = attribute(primitive, cgltf_attribute_type_position);
			if (!pos || pos->count < 3 || pos->count > 65535) fail("Each primitive requires 3..65535 vertices; split larger meshes.");
			accessor(pos, cgltf_type_vec3, pos->count);
			const auto* normal = attribute(primitive, cgltf_attribute_type_normal);
			const auto* uv = attribute(primitive, cgltf_attribute_type_texcoord);
			const auto* tangent = attribute(primitive, cgltf_attribute_type_tangent);
			const auto* color = attribute(primitive, cgltf_attribute_type_color);
			if (normal) accessor(normal, cgltf_type_vec3, pos->count);
			if (uv) accessor(uv, cgltf_type_vec2, pos->count);
			if (tangent) accessor(tangent, cgltf_type_vec4, pos->count);
			if (color) accessor(color, color->type == cgltf_type_vec3 ? cgltf_type_vec3 : cgltf_type_vec4, pos->count);
			const size_t count = primitive.indices ? primitive.indices->count : pos->count;
			if (!count || count % 3 || count > 3000000) fail("Expected complete triangles, at most 3000000 indices per primitive.");
			if (primitive.indices)
			{
				accessor(primitive.indices, cgltf_type_scalar, count);
				const auto type = primitive.indices->component_type;
				if (primitive.indices->normalized || (type != cgltf_component_type_r_8u &&
					type != cgltf_component_type_r_16u && type != cgltf_component_type_r_32u))
					fail("Triangle indices must be unsigned byte/short/int.");
			}
			const size_t vertexCount = normal ? pos->count : count;
			if (vertexCount > 65535) fail("Flat normals would exceed 65535 vertices; export normals or split the mesh.");
			geometryBytes += vertexCount*sizeof(YdrVertex) + count*sizeof(uint32_t);
			if (geometryBytes > 64*1024*1024 || out.parts.size() >= 128) fail("Geometry exceeds 64 MiB or 128 primitive instances.");
			GlbPart part;
			part.name = std::string(node->name ? node->name : "node_" + std::to_string(node - data->nodes)) +
				" / " + (node->mesh->name ? node->mesh->name : "mesh") + " / primitive_" + std::to_string(p);
			part.hasTangents = tangent != nullptr && normal != nullptr;
			auto& mesh = part.geometry;
			mesh.material = primitive.material ? static_cast<uint16_t>(primitive.material - data->materials) : defaultMaterial;
			if (!uv && out.materials[mesh.material].texture >= 0) fail("Textured mesh is missing TEXCOORD_0: " + part.name);
			mesh.vertices.resize(pos->count); mesh.indices.resize(count);
			for (size_t i = 0; i < pos->count; ++i)
			{
				auto& v = mesh.vertices[i];
				const auto source = read<3>(pos, i);
				auto world = direction(m, source);
				for (unsigned k = 0; k < 3; ++k) world[k] = finite(world[k] + m[k+12]);
				v.position = zUp(world);
				if (normal)
				{
					const auto n = read<3>(normal, i);
					V3 transformed;
					for (unsigned k = 0; k < 3; ++k) transformed[k] = (ca[k]*n[0] + cb[k]*n[1] + cc[k]*n[2]) / determinant;
					v.normal = zUp(normalize(transformed));
				}
				if (uv) v.uv = read<2>(uv, i);
				if (color)
				{
					const auto rgba = read<4>(color, i);
					for (unsigned k = 0; k < 4; ++k)
					{
						const float value = (k == 3 && color->type == cgltf_type_vec3) ? 1 : rgba[k];
						if (value < 0 || value > 1) fail("Vertex colors must be in 0..1.");
						v.color[k] = static_cast<uint8_t>(std::lround(value*255));
					}
				}
				if (part.hasTangents)
				{
					const auto t = read<4>(tangent, i);
					if (t[3] != 1 && t[3] != -1) fail("Tangent handedness must be +1 or -1.");
					auto transformed = zUp(direction(m, {t[0], t[1], t[2]}));
					const float projection = dot(transformed, v.normal);
					for (unsigned k = 0; k < 3; ++k) transformed[k] -= projection*v.normal[k];
					transformed = normalize(transformed);
					v.tangent = { transformed[0], transformed[1], transformed[2], t[3]*(determinant < 0 ? -1 : 1) };
				}
			}
			for (size_t i = 0; i < count; ++i)
			{
				const auto index = primitive.indices ? cgltf_accessor_read_index(primitive.indices, i) : i;
				if (index >= mesh.vertices.size()) fail("Triangle index is out of range.");
				mesh.indices[i] = static_cast<uint32_t>(index);
			}
			if (determinant < 0)
				for (size_t i = 0; i < count; i += 3) std::swap(mesh.indices[i+1], mesh.indices[i+2]);
			std::vector<YdrVertex> flat;
			if (!normal) flat.reserve(count);
			for (size_t i = 0; i < count; i += 3)
			{
				const auto& a0 = mesh.vertices[mesh.indices[i]].position;
				const auto& b0 = mesh.vertices[mesh.indices[i+1]].position;
				const auto& c0 = mesh.vertices[mesh.indices[i+2]].position;
				const auto face = normalize(cross({b0[0]-a0[0], b0[1]-a0[1], b0[2]-a0[2]},
					{c0[0]-a0[0], c0[1]-a0[1], c0[2]-a0[2]}));
				if (!normal)
					for (unsigned k = 0; k < 3; ++k) { auto v = mesh.vertices[mesh.indices[i+k]]; v.normal = face; flat.push_back(v); }
			}
			if (!normal)
			{
				mesh.vertices = std::move(flat);
				for (size_t i = 0; i < count; ++i) mesh.indices[i] = static_cast<uint32_t>(i);
				warning(out, "Missing normals: generated flat normals (vertices split at triangle edges).");
			}
			if (!part.hasTangents) warning(out, "Some parts have no usable tangents; tangent generation is required before normal-mapped export.");
			out.parts.push_back(std::move(part));
		}
	}
	if (out.parts.empty()) fail("The selected scene has no triangle meshes.");
	return out;
}

GlbModel loadGlb(const wxString& path)
{
	wxFile file(path, wxFile::read);
	if (!file.IsOpened()) fail("Could not open file.");
	const auto length = file.Length();
	if (length < 20 || length > wxFileOffset(fileLimit)) fail("File size must be 20 bytes..64 MiB.");
	std::vector<uint8_t> bytes(static_cast<size_t>(length));
	if (file.Read(bytes.data(), bytes.size()) != static_cast<wxFileOffset>(bytes.size())) fail("Could not read complete file.");
	return importGlb(bytes);
}

PreviewModel makeGlbPreview(const GlbModel& source, int selectedPart)
{
	if (selectedPart < -1 || selectedPart >= static_cast<int>(source.parts.size())) fail("Preview part index is out of range.");
	PreviewModel out;
	out.textures = source.textures;
	for (unsigned k = 0; k < 3; ++k) { out.bmin[k] = std::numeric_limits<float>::max(); out.bmax[k] = -out.bmin[k]; }
	size_t triangles = 0;
	for (size_t i = 0; i < source.parts.size(); ++i)
	{
		if (selectedPart >= 0 && i != size_t(selectedPart)) continue;
		const auto& part = source.parts[i];
		const auto& material = source.materials.at(part.geometry.material);
		PreviewMesh mesh;
		mesh.texture = material.texture; mesh.idx = part.geometry.indices;
		for (const auto& v : part.geometry.vertices)
		{
			for (unsigned k = 0; k < 3; ++k)
			{
				mesh.pos.push_back(v.position[k]); mesh.nrm.push_back(v.normal[k]);
				mesh.colors.push_back(v.color[k]/255.0f * material.baseColor[k]);
				out.bmin[k] = std::min(out.bmin[k], v.position[k]); out.bmax[k] = std::max(out.bmax[k], v.position[k]);
			}
			mesh.uv.insert(mesh.uv.end(), v.uv.begin(), v.uv.end());
		}
		triangles += mesh.idx.size()/3;
		out.meshes.push_back(std::move(mesh));
	}
	if (out.meshes.empty()) fail("No preview geometry.");
	out.info = "GLB (static), " + std::to_string(out.meshes.size()) + " parts, " + std::to_string(triangles) + " tris";
	return out;
}
