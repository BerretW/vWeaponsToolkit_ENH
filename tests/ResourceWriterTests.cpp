#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/zstream.h>
#include "cRscWriter.h"
#include "cYtdWriter.h"
#include "cModelPreview.h"
#include <functional>
#include <iostream>
#include <stdexcept>

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
