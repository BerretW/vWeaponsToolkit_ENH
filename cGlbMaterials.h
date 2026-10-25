#pragma once
#include "cYtdWriter.h"
#include <array>

struct cgltf_material;

enum class GlbMaterialAlpha { Opaque, Mask, Blend };

// A bounded artistic conversion, NOT equivalent glTF PBR rendering. Image
// pointers are non-owning and must remain valid through convertGlbMaterial().
// Retain source metadata: GlbMaterial alone currently loses that metadata.
struct GlbMaterialInput
{
	std::string name;
	std::array<float, 4> baseColorFactor = { 1, 1, 1, 1 };
	const YtdImage* baseColor = nullptr;
	GlbMaterialAlpha alpha = GlbMaterialAlpha::Opaque;
	const YtdImage* normal = nullptr;
	const YtdImage* metallicRoughness = nullptr;
	float normalScale = 1;
	float metallicFactor = 1;
	float roughnessFactor = 1;
	bool bakeSharedOcclusion = false;
	float occlusionStrength = 1;
};

struct GlbConvertedMaterial
{
	YtdImage diffuse;
	YtdImage normal;
	YtdImage specular;
};

// Validates the original cgltf metadata before constructing a bounded input.
// Accepts base color, tangent normal and metallic-roughness with independent
// resolutions; all views must use UV0, no transform and repeat wrapping.
// Occlusion is supported only when sharing the MR texture (as in test/m4a1.glb);
// other occlusion/emissive maps, PBR extensions, double-sided and non-opaque
// materials are rejected, never silently discarded. Image presence must match
// each source texture view, even if multiple views share an image.
GlbMaterialInput glbMaterialInput(const cgltf_material& material, const YtdImage* baseColor,
	const YtdImage* normal = nullptr, const YtdImage* metallicRoughness = nullptr);

// Every RGBA image must be power-of-two 1..4096; no resize/UV rewrite.
// Base color uses ColorSrgb, normal TangentNormal, MR LinearData semantics.
// OPAQUE explicitly discards factor/image alpha, per glTF. MASK/BLEND fail.
// Normal RGB decodes to [-1,1]; X/Y *= normalScale, then XYZ is normalized.
// glTF +Y tangent orientation is retained (no green-channel inversion).
// Artistic specular grayscale = (0.04 + 0.96*m) * (1 - 0.75*r),
// m = MR.B * metallicFactor, r = MR.G * roughnessFactor, channels in [0,1].
// MR.R/A are ignored by specular, NOT interpreted as opacity. No base-color
// tint, energy compensation or roughness-to-exponent mapping is attempted.
// Explicit shared-AO bake multiplies linear diffuse by
// 1 + occlusionStrength*(MR.R-1). At different resolutions MR.R is sampled
// nearest-neighbor at diffuse texel centers, with matching normalized UVs.
// This artistic bake is not equivalent to glTF's indirect-light-only occlusion.
// Output names: normalized <name>_d/_n/_s. Missing maps use 4x4 constants:
// baked white diffuse, (128,128,255,255) normal, factor-derived specular (MR=1).
// Each present map retains its own dimensions. Output alpha is always 255.
// Reuse buildYtd({result.diffuse,result.normal,result.specular}, generation)
// for BC encoding. Legacy still requires every map's dimensions >=4.
GlbConvertedMaterial convertGlbMaterial(const GlbMaterialInput& input);
