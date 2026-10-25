#pragma once
#include "cGlbPlacement.h"

struct GlbRigidMesh
{
	size_t sourcePart = 0;
	uint16_t bone = 0;
	// Aligned rest vertices in the assigned bone's local coordinate system.
	std::vector<std::array<float,3>> localPositions;
};

void validateGlbRigData(const GlbModel& model, const GlbImportProject& project);
void setGlbReferenceSkeleton(const GlbModel& model, GlbImportProject& project, const ReferenceSkeleton& skeleton);
void assignGlbBone(const GlbModel& model, GlbImportProject& project, size_t part, int bone);
void fitGlbBoneToPivot(const GlbModel& model, GlbImportProject& project, size_t part);
void moveGlbBoneOrigin(const GlbModel& model,GlbImportProject& project,size_t bone,const std::array<float,3>& outputPoint);
std::vector<int> suggestGlbBones(const GlbModel& model, const GlbImportProject& project);
std::vector<std::string> glbRigErrors(const GlbModel& model, const GlbImportProject& project);
std::vector<GlbRigidMesh> buildGlbRigidBindings(const GlbModel& model, const GlbImportProject& project);
PreviewModel makeGlbRigPreview(const GlbModel& model, const GlbImportProject& project,
	int selectedBone = -1, float degrees = 0, unsigned localAxis = 0, float slideMetres = 0);
