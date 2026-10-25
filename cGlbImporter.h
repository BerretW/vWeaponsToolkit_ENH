#pragma once
#include "cModelPreview.h"
#include "cYdrWriter.h"
#include <wx/string.h>

struct GlbMaterial
{
	std::string name;
	std::array<float, 4> baseColor = { 1, 1, 1, 1 };
	int texture = -1;
};

struct GlbPart
{
	std::string name;
	std::vector<std::string> nodeNames;
	size_t nodeIndex = 0;
	size_t meshIndex = 0;
	size_t primitiveIndex = 0;
	std::array<float, 16> sourceWorldTransform = {};
	YdrMesh geometry;
	bool hasTangents = false;
};

struct GlbModel
{
	std::string sourceSha256;
	std::vector<GlbPart> parts;
	std::vector<GlbMaterial> materials;
	std::vector<PreviewTexture> textures;
	std::vector<std::string> warnings;
};

// Static GLB 2.0 only, embedded buffers/images. Throws with a diagnostic on failure.
// Bakes scene transforms and converts glTF Y-up to Z-up: (x, y, z) -> (x, -z, y).
// Keeps metres and the source origin; does not assign weapon bones or GTA shaders.
GlbModel importGlb(const std::vector<uint8_t>& bytes);
GlbModel loadGlb(const wxString& path);
PreviewModel makeGlbPreview(const GlbModel& source, int selectedPart = -1);
PreviewModel makeGlbPreview(const GlbModel& source, const std::vector<size_t>& selectedParts);
