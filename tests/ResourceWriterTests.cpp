#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/zstream.h>
#include "cRscWriter.h"
#include "cYtdWriter.h"
#include "cYdrWriter.h"
#include "cModelPreview.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <limits>

void testGlbImporter(const wxString& output);

namespace
{
	void require(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	void fails(const std::function<void()>& action, const char* message)
	{
		try { action(); }
		catch (const std::exception& e)
		{
			require(std::string(e.what()).find(message) != std::string::npos, "Unexpected failure reason.");
			return;
		}
		throw std::runtime_error("Expected an explicit failure.");
	}

	uint32_t read32(const std::vector<uint8_t>& data, size_t offset)
	{
		require(offset + 4 <= data.size(), "Test reader out of bounds.");
		uint32_t value = 0;
		for (unsigned i = 0; i < 4; ++i) value |= uint32_t(data[offset + i]) << (8 * i);
		return value;
	}

	size_t pageSize(uint32_t flags)
	{
		const size_t units = ((flags >> 27) & 1) + (((flags >> 26) & 1) << 1) +
			(((flags >> 25) & 1) << 2) + (((flags >> 24) & 1) << 3) +
			(((flags >> 17) & 127) << 4) + (((flags >> 11) & 63) << 5) +
			(((flags >> 7) & 15) << 6) + (((flags >> 5) & 3) << 7) + (((flags >> 4) & 1) << 8);
		return (size_t(512) << (flags & 15)) * units;
	}

	void testContainer(size_t systemSize, size_t graphicsSize, const wxString& output)
	{
		cRscWriter writer;
		const auto root = writer.allocate(ResourceSegment::System, systemSize);
		require(root == 0x50000000, "Root address.");
		writer.u32(root + 4, 1);
		writer.u32(root + systemSize - 4, 0x12345678);
		if (graphicsSize)
		{
			const auto gpu = writer.allocate(ResourceSegment::Graphics, graphicsSize, 256);
			require(gpu == 0x60000000, "Graphics address.");
			writer.u32(gpu + graphicsSize - 4, 0x87654321);
		}
		const auto file = writer.build(165);
		require(file == writer.build(165), "Output must be deterministic.");
		require(read32(file, 0) == 0x37435352 && read32(file, 4) == 165, "RSC7 header.");
		const size_t ss = pageSize(read32(file, 8)), gs = pageSize(read32(file, 12));
		require(ss >= systemSize + 32 && gs >= graphicsSize, "Page sizing.");
		require((read32(file, 8) >> 28) == 10 && (read32(file, 12) >> 28) == 5, "Version nibbles.");
		wxMemoryInputStream compressed(file.data() + 16, file.size() - 16);
		wxZlibInputStream inflate(compressed, wxZLIB_NO_HEADER);
		std::vector<uint8_t> raw(ss + gs);
		inflate.Read(raw.data(), raw.size());
		require(inflate.LastRead() == raw.size(), "Raw DEFLATE size.");
		require(read32(raw, systemSize - 4) == 0x12345678, "System payload corrupted.");
		if (gs) require(read32(raw, ss + graphicsSize - 4) == 0x87654321, "Graphics payload corrupted.");
		const auto table = read32(raw, 8) & 0x0FFFFFFF;
		require(read32(raw, 12) == 0 && table >= systemSize && table % 16 == 0, "Page table pointer.");
		require(raw[table + 8] == 1 && raw[table + 9] == (graphicsSize ? 1 : 0), "Page counts.");
		if (!output.empty()) cRscWriter::save(output, file);
	}

	YtdImage solid(const std::string& name, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b)
	{
		YtdImage image;
		image.name = name; image.width = w; image.height = h;
		for (size_t i = 0; i < size_t(w) * h; ++i)
			image.rgba.insert(image.rgba.end(), { r, g, b, 255 });
		return image;
	}

	void testDrawableWriter(const wxString& output)
	{
		auto path = [&](const wxString& name) { return wxFileName(output, name).GetFullPath(); };
		YdrDrawable input;
		input.name = "W_TEST_STATIC";
		YdrMaterial material;
		material.diffuseTexture = "OPAQUE"; material.normalTexture = "flat_normal"; material.specularTexture = "black_spec";
		input.materials.push_back(material);
		material.diffuseTexture = "alpha"; material.bumpiness = 0.7f; material.wetness = 0.25f;
		material.specularIntensity = 0.5f; material.specularFalloff = 40; material.specularFresnel = 0.75f;
		input.materials.push_back(material);
		YdrMesh first;
		const std::array<float, 3> positions[4] = { {-2, -1, 0}, {0, -1, 0}, {0, 1, 0}, {-2, 1, 0} };
		const std::array<float, 2> coords[4] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
		for (unsigned i = 0; i < 4; ++i)
		{
			YdrVertex v; v.position = positions[i]; v.uv = coords[i];
			v.color = { 128, 64, 32, 255 };
			first.vertices.push_back(v);
		}
		first.indices = { 0, 1, 2, 0, 2, 3 };
		input.meshes.push_back(first);
		auto second = first;
		second.material = 1; second.indices = { 0, 2, 1, 0, 3, 2 };
		for (auto& v : second.vertices)
		{
			v.position[0] += 3; v.position[2] = 1;
			v.tangent = { -1, 0, 0, -1 }; v.color = { 25, 80, 150, 200 };
			v.uv[0] -= 0.25f; v.uv[1] *= 2;
		}
		input.meshes.push_back(second);
		auto alpha = solid("alpha", 8, 8, 40, 200, 60);
		for (size_t i = 3; i < alpha.rgba.size(); i += 4) alpha.rgba[i] = 128;
		const std::vector<YtdImage> textures = { solid("opaque", 8, 8, 200, 100, 50), alpha,
			solid("flat_normal", 4, 4, 128, 128, 255), solid("black_spec", 4, 4, 0, 0, 0) };
		for (const auto gen : { GameGeneration::Legacy, GameGeneration::Enhanced })
		{
			const wxString prefix = gen == GameGeneration::Legacy ? "gen8" : "gen9";
			const auto ytd = path(prefix + "_static.ytd");
			cRscWriter::save(ytd, buildYtd(textures, gen));
			const auto binary = buildYdr(input, gen);
			require(binary == buildYdr(input, gen), "Drawable output must be deterministic.");
			const auto ydr = path(prefix + "_static.ydr");
			cRscWriter::save(ydr, binary);
			PreviewModel preview;
			std::string error;
			const bool loaded = loadPreviewModel(ydr.ToStdString(), { ytd.ToStdString() }, preview, error);
			require(loaded, error.c_str());
			require(preview.meshes.size() == 2 && preview.textures.size() == 2, "New drawable mesh/material round trip.");
			for (size_t i = 0; i < input.meshes.size(); ++i)
			{
				const auto& actual = preview.meshes[i];
				const auto& expected = input.meshes[i];
				require(actual.idx == expected.indices && actual.texture >= 0, "Index/material round trip.");
				for (size_t v = 0; v < expected.vertices.size(); ++v)
				{
					for (unsigned k = 0; k < 3; ++k)
					{
						require(actual.pos[v * 3 + k] == expected.vertices[v].position[k], "Position round trip.");
						require(actual.nrm[v * 3 + k] == expected.vertices[v].normal[k], "Normal round trip.");
					}
					for (unsigned k = 0; k < 2; ++k)
						require(actual.uv[v * 2 + k] == expected.vertices[v].uv[k], "UV round trip.");
				}
			}
			require(preview.bmin[0] == -2 && preview.bmax[0] == 3 && preview.bmax[2] == 1, "New drawable bounds.");
			std::cout << prefix << " generated: " << preview.info << '\n';
			auto single = input;
			single.materials.resize(1); single.meshes.resize(1);
			cRscWriter::save(path(prefix + "_single.ydr"), buildYdr(single, gen));
			auto boundary = single;
			boundary.meshes[0].vertices.resize(65535, first.vertices[0]);
			boundary.meshes[0].vertices[65534] = first.vertices[2];
			boundary.meshes[0].indices = { 0, 1, 65534 };
			cRscWriter::save(path(prefix + "_boundary.ydr"), buildYdr(boundary, gen));
			boundary.meshes[0].vertices.push_back(first.vertices[0]);
			fails([&] { buildYdr(boundary, gen); }, "65535");
		}
		fails([&] { buildYdr(input, static_cast<GameGeneration>(-1)); }, "generation");
		auto bad = input; bad.name = "../bad";
		fails([&] { buildYdr(bad, GameGeneration::Legacy); }, "name");
		bad = input; bad.materials.clear();
		fails([&] { buildYdr(bad, GameGeneration::Legacy); }, "1..128");
		bad = input; bad.meshes.resize(129, first);
		fails([&] { buildYdr(bad, GameGeneration::Enhanced); }, "1..128");
		bad = input; bad.meshes[0].material = 2;
		fails([&] { buildYdr(bad, GameGeneration::Legacy); }, "material index");
		bad = input; bad.meshes[0].indices[0] = 65536;
		fails([&] { buildYdr(bad, GameGeneration::Legacy); }, "index is out of range");
		bad = input; bad.meshes[0].indices.pop_back();
		fails([&] { buildYdr(bad, GameGeneration::Enhanced); }, "complete triangles");
		bad = input; bad.meshes[0].indices[1] = 0;
		fails([&] { buildYdr(bad, GameGeneration::Legacy); }, "Degenerate");
		bad = input; bad.meshes[0].vertices[0].position[0] = std::numeric_limits<float>::quiet_NaN();
		fails([&] { buildYdr(bad, GameGeneration::Enhanced); }, "finite");
		bad = input; bad.meshes[0].vertices[0].uv[0] = std::numeric_limits<float>::infinity();
		fails([&] { buildYdr(bad, GameGeneration::Enhanced); }, "finite");
		bad = input; bad.meshes[0].vertices[0].normal = { 0, 0, 0 };
		fails([&] { buildYdr(bad, GameGeneration::Legacy); }, "unit vector");
		bad = input; bad.meshes[0].vertices[0].tangent = { 0, 0, 1, 1 };
		fails([&] { buildYdr(bad, GameGeneration::Enhanced); }, "perpendicular");
		bad = input; bad.meshes[0].vertices[0].tangent[3] = 0;
		fails([&] { buildYdr(bad, GameGeneration::Enhanced); }, "handedness");
		bad = input; bad.materials[0].normalTexture.clear();
		fails([&] { buildYdr(bad, GameGeneration::Enhanced); }, "Texture names");
		bad = input; bad.materials[0].specularIntensity = -1;
		fails([&] { buildYdr(bad, GameGeneration::Legacy); }, "nonnegative");
		bad = input; bad.lodDistance = 0;
		fails([&] { buildYdr(bad, GameGeneration::Legacy); }, "positive");
		std::cout << "Drawable validation/boundary tests passed.\n";
	}
}

int main(int argc, char** argv)
{
	try
	{
		wxInitializer wx;
		require(wx.IsOk(), "wxWidgets initialization failed.");
		require(argc == 3, "Usage: ResourceWriterTests <repo-root> <existing-output-directory>");
		const wxString repo = wxString::FromUTF8(argv[1]), output = wxString::FromUTF8(argv[2]);
		require(wxDirExists(output), "Output directory must exist.");
		auto path = [&](const wxString& name) { return wxFileName(output, name).GetFullPath(); };
		require(assetNameHash("DiffuseSampler") == 0xf1fe2b71, "Case-insensitive shader hashes.");
		testDrawableWriter(output);
		testGlbImporter(output);
		testContainer(64, 0, path("system_only.rsc"));
		testContainer(8192, 8193, path("page_growth.rsc"));
		testContainer(48, 16 * 1024 * 1024 + 1, path("large_page.rsc"));
		cRscWriter invalid;
		fails([&] { invalid.build(13); }, "ResourceFileBase");
		fails([&] { invalid.allocate(ResourceSegment::System, 1, 3); }, "alignment");
		fails([&] { invalid.allocate(ResourceSegment::System, 0); }, "allocation");
		fails([&] { invalid.allocate(static_cast<ResourceSegment>(-1), 16); }, "segment");
		fails([&] { invalid.allocate(ResourceSegment::System, 128 * 1024 * 1024 + 1); }, "limit");
		const auto address = invalid.allocate(ResourceSegment::System, 64);
		fails([&] { invalid.u32(address + 62, 0); }, "outside");
		fails([&] { invalid.u8(0x150000000, 0); }, "pointer");
		fails([&] { cRscWriter::save(path("empty.rsc"), {}); }, "empty");
		cRscWriter aligned;
		aligned.allocate(ResourceSegment::System, 17);
		require(aligned.allocate(ResourceSegment::System, 8, 512) == 0x50000200, "Allocation alignment.");

		auto opaque = solid("OPAQUE", 8, 4, 0, 0, 0);
		for (size_t i = 0; i < 32; ++i)
		{
			const auto x = static_cast<uint8_t>(i % 8);
			opaque.rgba[i * 4] = 32 + x * 24;
			opaque.rgba[i * 4 + 1] = 16 + x * 16;
			opaque.rgba[i * 4 + 2] = 8 + x * 8;
		}
		auto alpha = solid("alpha", 4, 4, 200, 40, 20);
		for (size_t i = 0; i < 16; ++i) alpha.rgba[i * 4 + 3] = static_cast<uint8_t>(i * 17);
		auto tiny = solid("tiny", 1, 1, 0, 255, 0);
		auto strip = solid("strip", 1, 8, 40, 80, 120);
		auto transparent = solid("transparent", 2, 2, 200, 50, 100);
		for (size_t i = 3; i < transparent.rgba.size(); i += 4) transparent.rgba[i] = 0;
		const std::vector<YtdImage> images = { opaque, alpha, tiny, strip, transparent };
		auto legacyImages = images;
		legacyImages[2] = solid("tiny", 4, 4, 0, 255, 0);
		legacyImages[3] = solid("strip", 4, 8, 40, 80, 120);
		legacyImages[4] = solid("transparent", 4, 4, 200, 50, 100);
		for (size_t i = 3; i < legacyImages[4].rgba.size(); i += 4) legacyImages[4].rgba[i] = 0;
		const auto legacy = buildYtd(legacyImages, GameGeneration::Legacy);
		const auto enhanced = buildYtd(images, GameGeneration::Enhanced);
		require(legacy == buildYtd(legacyImages, GameGeneration::Legacy), "YTD output must be deterministic.");
		cRscWriter::save(path("legacy.ytd"), legacy);
		cRscWriter::save(path("enhanced.ytd"), enhanced);
		cRscWriter::save(path(wxString::FromUTF8("textury_\xC4\x8D.ytd")), legacy);
		fails([&] { buildYtd({}, GameGeneration::Legacy); }, "1..1024");
		fails([&] { buildYtd(images, static_cast<GameGeneration>(-1)); }, "generation");
		fails([&] { buildYtd({ tiny }, GameGeneration::Legacy); }, "at least 4x4");
		fails([&] { buildYtd({ opaque, opaque }, GameGeneration::Legacy); }, "Duplicate");
		auto bad = opaque; bad.name = "opaque";
		fails([&] { buildYtd({ opaque, bad }, GameGeneration::Legacy); }, "Duplicate");
		bad.name = "../invalid";
		fails([&] { buildYtd({ bad }, GameGeneration::Legacy); }, "name");
		bad.name = "script_rt_test";
		fails([&] { buildYtd({ bad }, GameGeneration::Enhanced); }, "script_rt_");
		bad.name = "valid"; bad.width = 3;
		fails([&] { buildYtd({ bad }, GameGeneration::Legacy); }, "powers of two");
		bad.width = 8192;
		fails([&] { buildYtd({ bad }, GameGeneration::Legacy); }, "powers of two");
		bad = opaque; bad.rgba.pop_back();
		fails([&] { buildYtd({ bad }, GameGeneration::Legacy); }, "RGBA byte count");

		const std::vector<YtdImage> weaponTextures = {
			solid("w_ar_carbinerifle_m31", 8, 8, 200, 100, 50),
			solid("w_ar_carbinerifle_m31_decalw_ar_carbinerifle_m31_decal_am", 4, 4, 40, 200, 50),
			solid("w_ar_cr_notes_m31_d", 4, 4, 50, 40, 200)
		};
		for (const auto gen : { GameGeneration::Legacy, GameGeneration::Enhanced })
		{
			const wxString folder = gen == GameGeneration::Legacy ? "gen8" : "gen9";
			const wxString dictionary = path(folder + "_weapon.ytd");
			cRscWriter::save(dictionary, buildYtd(weaponTextures, gen));
			for (const wxString suffix : { wxString(""), wxString("_hi") })
			{
				PreviewModel model;
				std::string error;
				const wxString drawable = repo + "\\test\\" + folder + "\\w_ar_carbinerifle_m31" + suffix + ".ydr";
				const bool loaded = loadPreviewModel(drawable.ToStdString(), { dictionary.ToStdString() }, model, error);
				require(loaded, error.c_str());
				require(model.textures.size() == 3, "Preview failed to resolve all newly written textures.");
				require(model.meshes.size() >= 4, "Sample geometry missing.");
				for (const auto& mesh : model.meshes) require(mesh.texture >= 0, "Mesh has no texture.");
				std::cout << folder << suffix << ": " << model.info << '\n';
			}
		}
		std::cout << "All native resource writer tests passed.\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "FAILED: " << e.what() << '\n';
		return 1;
	}
}
