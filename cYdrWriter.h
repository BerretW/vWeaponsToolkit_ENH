#pragma once
#include "cAssetFormat.h"
#include "cSkeleton.h"
#include <array>
#include <vector>

struct YdrVertex
{
	std::array<float, 3> position = {};
	std::array<float, 3> normal = { 0, 0, 1 };
	std::array<uint8_t, 4> color = { 255, 255, 255, 255 };
	std::array<float, 2> uv = {};
	std::array<float, 4> tangent = { 1, 0, 0, 1 };
};

enum class YdrShaderProfile { NormalSpecDecal, WeaponNormalSpecDetailPalette };

// Texture names are external YTD references.
struct YdrMaterial
{
	std::string diffuseTexture;
	std::string normalTexture;
	std::string specularTexture;
	float bumpiness = 1;
	float wetness = 1;
	float specularIntensity = 0.1f;
	float specularFalloff = 20;
	float specularFresnel = 0.99f;
	YdrShaderProfile profile = YdrShaderProfile::NormalSpecDecal;
	std::string detailTexture;
	std::string paletteTexture;
};

struct YdrMesh
{
	std::vector<YdrVertex> vertices;
	std::vector<uint32_t> indices;
	uint16_t material = 0;
	int rigidBone = -1;
	size_t sourcePart = size_t(-1);
};

struct YdrDrawable
{
	std::string name;
	std::vector<YdrMaterial> materials;
	std::vector<YdrMesh> meshes;
	float lodDistance = 9998;
	ReferenceSkeleton skeleton;
};

// High-LOD-only drawable; optional binary-reference skeleton and rigid per-mesh skinning.
// No collision or automatic LOD generation.
// Rejects invalid data instead of dropping triangles or truncating 32-bit indices.
std::vector<uint8_t> buildYdr(const YdrDrawable& drawable, GameGeneration generation);
