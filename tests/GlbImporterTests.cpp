#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/mstream.h>
#include "cGlbImporter.h"
#include "cRscWriter.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>

namespace
{
	void require(bool condition, const char* text)
	{
		if (!condition) throw std::runtime_error(text);
	}
	void checkNear(float actual, float expected)
	{
		require(std::fabs(actual-expected) < 0.0001f, "GLB numeric round trip failed.");
	}
	void replace(std::string& text, const std::string& from, const std::string& to)
	{
		const auto at = text.find(from);
		require(at != std::string::npos, "Fixture replacement missing.");
		text.replace(at, from.size(), to);
	}
	void put32(std::vector<uint8_t>& bytes, uint32_t value)
	{
		for (unsigned k = 0; k < 4; ++k) bytes.push_back(static_cast<uint8_t>(value >> (k*8)));
	}
	void putFloat(std::vector<uint8_t>& bytes, float value)
	{
		uint32_t bits;
		std::memcpy(&bits, &value, 4); put32(bytes, bits);
	}
	using Edit = std::function<void(std::string&)>;
	std::vector<uint8_t> fixture(const Edit& edit = {}, bool jpeg = false, size_t vertexCount = 3)
	{
		std::vector<uint8_t> bin;
		for (size_t i = 0; i < vertexCount; ++i)
		{
			putFloat(bin, i == 1 ? 1.f : 0.f); putFloat(bin, (i == 2 || i+1 == vertexCount) ? 1.f : 0.f); putFloat(bin, 0);
			const float n = 1/std::sqrt(2.f);
			putFloat(bin, n); putFloat(bin, n); putFloat(bin, 0);
			bin.insert(bin.end(), {32, 64, 128, 255});
			putFloat(bin, i == 1 ? 1.f : 0.f); putFloat(bin, i == 2 ? 1.f : 0.f);
			putFloat(bin, 0); putFloat(bin, 0); putFloat(bin, 1); putFloat(bin, 1);
		}
		const auto vertexBytes = bin.size();
		const auto lastIndex = static_cast<uint16_t>(vertexCount-1);
		bin.insert(bin.end(), {0, 0, 1, 0, static_cast<uint8_t>(lastIndex), static_cast<uint8_t>(lastIndex>>8), 0, 0});
		wxImage pixels(2, 2);
		pixels.SetRGB(wxRect(0, 0, 2, 2), 20, 40, 60);
		pixels.InitAlpha();
		std::fill_n(pixels.GetAlpha(), 4, uint8_t(128));
		wxMemoryOutputStream image;
		require(pixels.SaveFile(image, jpeg ? wxBITMAP_TYPE_JPEG : wxBITMAP_TYPE_PNG), "Fixture image save failed.");
		const auto imageSize = image.GetSize();
		const auto imageOffset = bin.size();
		bin.resize(imageOffset+imageSize);
		image.CopyTo(bin.data()+imageOffset, imageSize);
		std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"name":"weapon_root","translation":[10,20,30],"children":[1,2]},
{"name":"body","mesh":0,"translation":[1,2,3],"scale":[-2,3,4]},
{"name":"magazine","mesh":0,"translation":[-1,0,0]}],
"meshes":[{"name":"receiver","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"COLOR_0":2,"TEXCOORD_0":3,"TANGENT":4},"indices":5,"material":0}]}],
"materials":[{"name":"metal","pbrMetallicRoughness":{"baseColorFactor":[0.5,1,1,1],"baseColorTexture":{"index":0}}}],
"textures":[{"source":0}],"images":[{"bufferView":2,"mimeType":"image/png"}],
"accessors":[{"bufferView":0,"byteOffset":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":0,"byteOffset":24,"componentType":5121,"normalized":true,"count":3,"type":"VEC4"},
{"bufferView":0,"byteOffset":28,"componentType":5126,"count":3,"type":"VEC2"},
{"bufferView":0,"byteOffset":36,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":156,"byteStride":52},
{"buffer":0,"byteOffset":156,"byteLength":6},
{"buffer":0,"byteOffset":164,"byteLength":IMAGE_LENGTH}],
"buffers":[{"byteLength":BIN_LENGTH}]})";
		replace(json, "IMAGE_LENGTH", std::to_string(imageSize));
		replace(json, "BIN_LENGTH", std::to_string(bin.size()));
		replace(json, "\"byteLength\":156", "\"byteLength\":" + std::to_string(vertexBytes));
		replace(json, "\"byteOffset\":156", "\"byteOffset\":" + std::to_string(vertexBytes));
		replace(json, "\"byteOffset\":164", "\"byteOffset\":" + std::to_string(imageOffset));
		if (vertexCount != 3)
		{
			size_t at = 0;
			while ((at = json.find("\"count\":3", at)) != std::string::npos)
			{
				const auto text = "\"count\":" + std::to_string(vertexCount);
				json.replace(at, 9, text); at += text.size();
			}
			replace(json, "\"componentType\":5123,\"count\":" + std::to_string(vertexCount), "\"componentType\":5123,\"count\":3");
		}
		if (jpeg) replace(json, "image/png", "image/jpeg");
		if (edit) edit(json);
		while (json.size()%4) json += ' ';
		while (bin.size()%4) bin.push_back(0);
		std::vector<uint8_t> result;
		put32(result, 0x46546C67); put32(result, 2); put32(result, static_cast<uint32_t>(28+json.size()+bin.size()));
		put32(result, static_cast<uint32_t>(json.size())); put32(result, 0x4E4F534A);
		result.insert(result.end(), json.begin(), json.end());
		put32(result, static_cast<uint32_t>(bin.size())); put32(result, 0x004E4942);
		result.insert(result.end(), bin.begin(), bin.end());
		return result;
	}
	void fails(const std::function<void()>& fn, const std::string& reason)
	{
		try { fn(); }
		catch (const std::exception& e)
		{
			if (std::string(e.what()).find(reason) == std::string::npos)
				throw std::runtime_error("Expected GLB rejection containing '" + reason + "', got: " + e.what());
			return;
		}
		throw std::runtime_error("Expected a GLB import failure.");
	}
}

void testGlbImporter(const wxString& output)
{
	wxInitAllImageHandlers();
	auto bytes = fixture();
	const auto model = importGlb(bytes);
	require(model.parts.size() == 2 && model.textures.size() == 1, "GLB mesh instances/image sharing.");
	require(model.parts[0].name == "body / receiver / primitive_0", "GLB part names.");
	require(model.parts[1].name == "magazine / receiver / primitive_0", "GLB instance names.");
	const auto& body = model.parts[0];
	require(body.hasTangents && body.geometry.material == 0, "GLB tangent/material metadata.");
	require(body.geometry.indices == std::vector<uint32_t>({0, 2, 1}), "Mirrored node winding.");
	const auto& v = body.geometry.vertices[0];
	checkNear(v.position[0], 11); checkNear(v.position[1], -33); checkNear(v.position[2], 22);
	checkNear(body.geometry.vertices[1].position[0], 9); checkNear(body.geometry.vertices[2].position[2], 25);
	checkNear(v.normal[0], -3/std::sqrt(13.f)); checkNear(v.normal[1], 0); checkNear(v.normal[2], 2/std::sqrt(13.f));
	checkNear(v.tangent[0], 0); checkNear(v.tangent[1], -1); checkNear(v.tangent[2], 0); checkNear(v.tangent[3], -1);
	require(v.color == std::array<uint8_t, 4>({32, 64, 128, 255}), "Normalized vertex color.");
	require(body.geometry.vertices[1].uv == std::array<float, 2>({1, 0}), "GLB UV orientation.");
	require(model.textures[0].width == 2 && model.textures[0].height == 2, "Embedded image dimensions.");
	require(std::vector<uint8_t>(model.textures[0].rgba.begin(), model.textures[0].rgba.begin()+4) ==
		std::vector<uint8_t>({20, 40, 60, 128}), "Embedded PNG alpha/color.");
	const auto preview = makeGlbPreview(model, 0);
	require(preview.meshes.size() == 1 && makeGlbPreview(model).meshes.size() == 2, "GLB part selection.");
	checkNear(preview.meshes[0].colors[0], 32/255.f*0.5f);
	checkNear(preview.bmin[0], 9); checkNear(preview.bmax[0], 11); checkNear(preview.bmin[1], -33); checkNear(preview.bmax[2], 25);
	fails([&] { makeGlbPreview(model, 2); }, "part index");
	const auto path = wxFileName(output, wxString::FromUTF8("import_\xC5\xBE\x6C\x75\xC5\xA5.glb")).GetFullPath();
	cRscWriter::save(path, bytes);
	require(loadGlb(path).parts.size() == 2, "Unicode GLB path.");
	cRscWriter::save(wxFileName(output, "static_import.glb").GetFullPath(), bytes);
	const auto jpeg = importGlb(fixture({}, true));
	require(jpeg.textures.size() == 1 && jpeg.textures[0].rgba[3] == 255, "Embedded JPEG decoding.");
	const auto flat = importGlb(fixture([](std::string& j) { replace(j, "\"NORMAL\":1,", ""); }));
	require(!flat.parts[0].hasTangents && flat.parts[0].geometry.vertices.size() == 3, "Flat normal generation.");
	checkNear(flat.parts[0].geometry.vertices[0].normal[1], -1);
	const auto nonIndexed = importGlb(fixture([](std::string& j) { replace(j, "\"indices\":5,", ""); }));
	require(nonIndexed.parts[0].geometry.indices == std::vector<uint32_t>({0, 2, 1}), "Non-indexed triangles.");
	const auto defaults = importGlb(fixture([](std::string& j) { replace(j, ",\"material\":0", ""); }));
	require(defaults.materials[defaults.parts[0].geometry.material].name == "default", "Default glTF material.");
	const auto boundary = importGlb(fixture({}, false, 65535));
	require(boundary.parts[0].geometry.vertices.size() == 65535, "GLB 65535-vertex boundary.");
	require(boundary.parts[0].geometry.indices == std::vector<uint32_t>({0, 65534, 1}), "GLB high index must not truncate.");
	checkNear(boundary.parts[0].geometry.vertices[65534].position[2], 25);
	fails([&] { importGlb(fixture({}, false, 65536)); }, "65535 vertices");
	auto instances = [](size_t count) {
		return fixture([&](std::string& j) {
			const auto begin = j.find("\"nodes\":[{");
			const auto end = j.find("\"meshes\":");
			require(begin != std::string::npos && end != std::string::npos, "Scene fixture bounds.");
			std::string nodes = "\"nodes\":[{\"children\":[";
			for (size_t i = 1; i <= count; ++i) nodes += (i > 1 ? "," : "") + std::to_string(i);
			nodes += "]}";
			for (size_t i = 0; i < count; ++i) nodes += ",{\"mesh\":0}";
			nodes += "],\n";
			j.replace(begin, end-begin, nodes);
		});
	};
	require(importGlb(instances(128)).parts.size() == 128, "GLB 128-part boundary.");
	fails([&] { importGlb(instances(129)); }, "128 primitive");
	fails([&] { importGlb({}); }, "GLB 2.0");
	auto truncated = bytes; truncated.pop_back();
	fails([&] { importGlb(truncated); }, "valid length");
	fails([&] { importGlb(fixture([](std::string& j) { replace(j, "\"asset\":", "\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],\"asset\":"); })); }, "required extension");
	fails([&] { importGlb(fixture([](std::string& j) { replace(j, "\"buffers\":[{", "\"buffers\":[{\"uri\":\"outside.bin\","); })); }, "embedded binary");
	fails([&] { importGlb(fixture([](std::string& j) { replace(j, "\"images\":[{", "\"images\":[{\"uri\":\"outside.png\","); })); }, "image URIs");
	fails([&] { importGlb(fixture([](std::string& j) { replace(j, "\"attributes\":{", "\"mode\":1,\"attributes\":{"); })); }, "Only triangle");
	fails([&] { importGlb(fixture([](std::string& j) { replace(j, "[-2,3,4]", "[0,3,4]"); })); }, "Singular");
	fails([&] { importGlb(fixture([](std::string& j) { replace(j, "\"TEXCOORD_0\":3,", ""); })); }, "missing TEXCOORD");
	fails([&] { importGlb(fixture([](std::string& j) { replace(j, "\"byteLength\":156", "\"byteLength\":99999999"); })); }, "Validation");
	fails([&] { importGlb(fixture([](std::string& j) { replace(j, "\"baseColorFactor\":[0.5", "\"baseColorFactor\":[2"); })); }, "Base color factor");
	fails([&] { importGlb(fixture([](std::string& j) { replace(j, "\"baseColorTexture\":{\"index\":0}", "\"baseColorTexture\":{\"index\":0,\"texCoord\":1}"); })); }, "TEXCOORD_0");
	fails([&] { importGlb(fixture([](std::string& j) {
		replace(j, "\"nodes\":[{", "\"skins\":[{\"joints\":[2]}],\"nodes\":[{");
		replace(j, "\"name\":\"body\",\"mesh\":0", "\"name\":\"body\",\"mesh\":0,\"skin\":0");
	})); }, "Skinned");
	fails([&] { importGlb(fixture([](std::string& j) {
		replace(j, "\"POSITION\":0", "\"POSITION\":0,\"WEIGHTS_1\":2");
	})); }, "Bone weights");
	fails([&] { importGlb(fixture([](std::string& j) {
		replace(j, "\"attributes\":{", "\"targets\":[{\"POSITION\":0}],\"attributes\":{");
	})); }, "morph targets");
	fails([&] { importGlb(fixture([](std::string& j) {
		replace(j, "\"type\":\"VEC3\",\"min\"", "\"type\":\"VEC3\",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":1,\"componentType\":5121},\"values\":{\"bufferView\":0}},\"min\"");
	})); }, "sparse accessor");
	auto invalidIndex = bytes;
	const size_t jsonLength = size_t(bytes[12]) | (size_t(bytes[13])<<8) | (size_t(bytes[14])<<16) | (size_t(bytes[15])<<24);
	const size_t bin = 28+jsonLength;
	invalidIndex[bin+156] = 3;
	fails([&] { importGlb(invalidIndex); }, "Validation");
	auto nan = bytes;
	const float value = std::numeric_limits<float>::quiet_NaN();
	std::memcpy(nan.data()+bin, &value, 4);
	fails([&] { importGlb(nan); }, "Non-finite");
	auto degenerate = bytes;
	std::fill_n(degenerate.data()+bin+52, 12, uint8_t(0));
	fails([&] { importGlb(degenerate); }, "degenerate");
	std::cout << "Static GLB transforms/materials/images/validation tests passed.\n";
}
