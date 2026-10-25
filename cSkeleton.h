#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <wx/string.h>

struct SkeletonBone
{
	std::string name;
	uint16_t tag = 0;
	int parent = -1;
	std::array<float, 3> translation = {};
	std::array<float, 4> rotation = {0,0,0,1};
	int sibling = -1;
	uint16_t flags = 0x1077;
	std::array<float,4> transformExtra = {};
};

struct ReferenceSkeleton
{
	std::vector<SkeletonBone> bones;
	bool binaryMetadata = false;
	uint32_t unknown1C = 0;
	std::array<uint32_t,3> signatures = {};
	uint16_t unknown5C = 1;
	uint16_t tagBucketCount = 0;
	std::vector<std::array<uint16_t,2>> childPairs;
};

// Original reader of local RSC7 fixtures: gen8/gen9 share the skeleton layout.
ReferenceSkeleton loadReferenceSkeleton(const wxString& ydrPath);
void validateReferenceSkeleton(const ReferenceSkeleton& skeleton);
using BoneMatrix = std::array<double, 16>;
std::vector<BoneMatrix> skeletonWorldMatrices(const ReferenceSkeleton& skeleton);
std::array<float, 3> bonePoint(const BoneMatrix& matrix, const std::array<float, 3>& point);
std::array<float, 3> inverseBonePoint(const BoneMatrix& matrix, const std::array<float, 3>& point);
std::array<float, 3> inverseBoneDirection(const BoneMatrix& matrix, const std::array<float, 3>& direction);
std::array<float, 3> boneDirection(const BoneMatrix& matrix, const std::array<float, 3>& direction);
