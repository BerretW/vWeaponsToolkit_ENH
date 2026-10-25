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
	YdrMesh geometry;
	bool hasTangents = false;
};

struct GlbModel
{
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
