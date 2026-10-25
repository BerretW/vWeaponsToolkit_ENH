#pragma once
#include "cGlbImportProject.h"

struct GlbMotionPreview
{
	float rotationDegrees = 0;
	float slideMetres = 0;
};

// Source coordinates are the importer's Z-up metres, before this alignment.
// Output = offset + Rz * Ry * Rx * (scale * (source - origin)).
void validateGlbPlacement(const GlbImportProject& project);
std::array<float, 3> glbAlignedPoint(const GlbAlignment& alignment, const std::array<float, 3>& point);
std::array<float, 3> glbSourcePoint(const GlbAlignment& alignment, const std::array<float, 3>& point);
std::vector<std::string> glbPlacementErrors(const GlbModel& model, const GlbImportProject& project);
PreviewModel makePreparedGlbPreview(const GlbModel& model, const GlbImportProject& project,
	int selectedPart = -1, const GlbMotionPreview& motion = {});
