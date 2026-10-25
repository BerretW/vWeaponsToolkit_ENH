#pragma once
#include "cGlbRig.h"
#include "cYtdWriter.h"

struct GlbBuiltAssets
{
	YdrDrawable drawable;
	std::vector<YtdImage> images;
	std::vector<std::string> diagnostics;
};

struct GlbComponentAsset
{
	GlbPartRole role;
	std::string attachBone;
	std::string localAttachBone;
	YdrDrawable drawable;
};

struct GlbPartitionedAssets
{
	YdrDrawable weapon;
	std::vector<GlbComponentAsset> components;
};

GlbBuiltAssets buildGlbAssets(const GlbModel& model,const GlbImportProject& project,unsigned textureSize=1024);
GlbPartitionedAssets partitionGlbComponents(const GlbBuiltAssets& assets,const GlbImportProject& project);
