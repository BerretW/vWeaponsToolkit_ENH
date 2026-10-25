#pragma once
#include "cGlbAssetBuilder.h"
#include "cWeaponResource.h"

// Publishes a new directory containing Legacy and Enhanced resources. Never overwrites.
// Conversion/metadata failures leave no success-shaped partial published directory.
std::vector<std::string> exportGlbResources(const GlbModel& model,const GlbImportProject& project,
	const wxString& destination,const wxString& templatesRoot,const WeaponResourceSettings& settings,
	unsigned textureSize=1024);
