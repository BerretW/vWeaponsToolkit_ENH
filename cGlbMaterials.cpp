// SPDX-License-Identifier: GPL-3.0-or-later
#include "cGlbMaterials.h"
#include "third_party/cgltf.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
	[[noreturn]] void fail(const char* message)
	{
		throw std::invalid_argument(std::string("GLB material: ") + message);
	}

	float linear(float s)
	{
		return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
	}

	uint8_t srgb(float v)
	{
		const float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
		return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, s * 255 + 0.5f)));
	}

	YtdImage solid(const std::string& name, std::array<uint8_t, 4> value, YtdImageSemantic semantic)
	{
		YtdImage result{ name, 4, 4, {}, semantic };
		for (unsigned i = 0; i < 16; ++i)
			result.rgba.insert(result.rgba.end(), value.begin(), value.end());
		return result;
	}

	void validateImage(const YtdImage& image, YtdImageSemantic semantic)
	{
		if (!image.width || !image.height || image.width > 4096 || image.height > 4096 ||
			(image.width & (image.width - 1)) || (image.height & (image.height - 1)) ||
			image.rgba.size() != size_t(image.width) * image.height * 4)
			fail("Every map requires RGBA and power-of-two dimensions, 1..4096.");
		if (image.semantic != semantic) fail("Image semantic does not match its material texture role.");
	}

	void validateView(const cgltf_texture_view& view, const YtdImage* image)
	{
		if (bool(view.texture) != bool(image))
			fail("Decoded image does not match the source material texture view.");
		if (!view.texture) return;
		if (view.texcoord != 0 || view.has_transform)
			fail("All material maps require TEXCOORD_0 without a texture transform.");
		const auto* sampler = view.texture->sampler;
		if (sampler && (sampler->wrap_s != cgltf_wrap_mode_repeat || sampler->wrap_t != cgltf_wrap_mode_repeat))
			fail("Only repeat texture wrapping is supported.");
	}

	void unitFactor(float factor)
	{
		if (!std::isfinite(factor) || factor < 0 || factor > 1)
			fail("Base color, metallic and roughness factors must be finite and in 0..1.");
	}
}

GlbMaterialInput glbMaterialInput(const cgltf_material& material, const YtdImage* baseColor,
	const YtdImage* normal, const YtdImage* metallicRoughness)
{
	if (material.alpha_mode != cgltf_alpha_mode_opaque)
		fail("MASK/BLEND alpha modes are unsupported by the opaque weapon profile.");
	if (material.double_sided) fail("Double-sided materials require a different shader profile.");
	if (material.has_pbr_specular_glossiness || material.has_clearcoat || material.has_transmission ||
		material.has_volume || material.has_ior || material.has_specular || material.has_sheen ||
		material.has_emissive_strength || material.has_iridescence || material.has_diffuse_transmission ||
		material.has_anisotropy || material.has_dispersion || material.unlit || material.extensions_count)
		fail("Material extensions are unsupported by the bounded weapon profile.");
	const auto& pbr = material.pbr_metallic_roughness;
	if (material.emissive_texture.texture || material.emissive_factor[0] != 0 ||
		material.emissive_factor[1] != 0 || material.emissive_factor[2] != 0)
		fail("Emissive conversion is unsupported by the bounded weapon profile.");
	const bool sharedOcclusion = material.occlusion_texture.texture != nullptr;
	if (sharedOcclusion)
	{
		if (material.occlusion_texture.texture != pbr.metallic_roughness_texture.texture)
			fail("Occlusion must share the metallic-roughness texture; separate AO images are unsupported.");
		validateView(material.occlusion_texture, metallicRoughness);
		unitFactor(material.occlusion_texture.scale);
	}
	validateView(pbr.base_color_texture, baseColor);
	validateView(material.normal_texture, normal);
	validateView(pbr.metallic_roughness_texture, metallicRoughness);
	for (float factor : pbr.base_color_factor) unitFactor(factor);
	unitFactor(pbr.metallic_factor);
	unitFactor(pbr.roughness_factor);
	if (!std::isfinite(material.normal_texture.scale)) fail("Normal scale must be finite.");
	if (baseColor) validateImage(*baseColor, YtdImageSemantic::ColorSrgb);
	if (normal) validateImage(*normal, YtdImageSemantic::TangentNormal);
	if (metallicRoughness) validateImage(*metallicRoughness, YtdImageSemantic::LinearData);
	GlbMaterialInput input;
	input.name = material.name ? material.name : "material";
	std::copy_n(pbr.base_color_factor, 4, input.baseColorFactor.begin());
	input.baseColor = baseColor;
	input.normal = normal;
	input.metallicRoughness = metallicRoughness;
	input.normalScale = material.normal_texture.scale;
	input.metallicFactor = pbr.metallic_factor;
	input.roughnessFactor = pbr.roughness_factor;
	input.bakeSharedOcclusion = sharedOcclusion;
	input.occlusionStrength = sharedOcclusion ? material.occlusion_texture.scale : 1;
	return input;
}

GlbConvertedMaterial convertGlbMaterial(const GlbMaterialInput& input)
{
	if (input.alpha != GlbMaterialAlpha::Opaque)
		fail("MASK/BLEND alpha modes are unsupported by the opaque weapon profile.");
	for (float factor : input.baseColorFactor) unitFactor(factor);
	unitFactor(input.metallicFactor);
	unitFactor(input.roughnessFactor);
	unitFactor(input.occlusionStrength);
	if (!std::isfinite(input.normalScale)) fail("Normal scale must be finite.");
	if (input.bakeSharedOcclusion && !input.metallicRoughness)
		fail("Shared occlusion bake requires the metallic-roughness image.");
	if (input.metallicRoughness) validateImage(*input.metallicRoughness, YtdImageSemantic::LinearData);
	const auto name = normalizeAssetName(input.name, "Material");
	if (name.size() > 125) fail("Material names must leave room for a two-character texture suffix.");
	GlbConvertedMaterial result;
	if (input.baseColor)
	{
		const auto& image = *input.baseColor;
		validateImage(image, YtdImageSemantic::ColorSrgb);
		result.diffuse = image;
	}
	else result.diffuse = solid("", { 255, 255, 255, 255 }, YtdImageSemantic::ColorSrgb);
	result.diffuse.name = name + "_d";
	for (size_t p = 0; p < result.diffuse.rgba.size(); p += 4)
	{
		float ao = 1;
		if (input.bakeSharedOcclusion)
		{
			const auto& mr = *input.metallicRoughness;
			const size_t x = (p / 4) % result.diffuse.width, y = (p / 4) / result.diffuse.width;
			const size_t mx = (2*x + 1) * mr.width / (2*result.diffuse.width);
			const size_t my = (2*y + 1) * mr.height / (2*result.diffuse.height);
			ao += input.occlusionStrength * (mr.rgba[(my * mr.width + mx) * 4] / 255.0f - 1);
		}
		for (unsigned c = 0; c < 3; ++c)
			result.diffuse.rgba[p + c] = srgb(linear(result.diffuse.rgba[p + c] / 255.0f) * input.baseColorFactor[c] * ao);
		result.diffuse.rgba[p + 3] = 255;
	}
	result.normal = solid(name + "_n", { 128, 128, 255, 255 }, YtdImageSemantic::TangentNormal);
	if (input.normal)
	{
		validateImage(*input.normal, YtdImageSemantic::TangentNormal);
		result.normal = *input.normal;
		result.normal.name = name + "_n";
		for (size_t p = 0; p < result.normal.rgba.size(); p += 4)
		{
			double n[3];
			for (unsigned c = 0; c < 3; ++c)
				n[c] = (result.normal.rgba[p + c] / 127.5 - 1) * (c < 2 ? double(input.normalScale) : 1.0);
			const double length = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
			if (length > 0.000001)
				for (double& c : n) c /= length;
			else { n[0] = 0; n[1] = 0; n[2] = 1; }
			for (unsigned c = 0; c < 3; ++c)
				result.normal.rgba[p + c] = static_cast<uint8_t>(std::max(0.0, std::min(255.0, (n[c] + 1) * 127.5 + 0.5)));
			result.normal.rgba[p + 3] = 255;
		}
	}
	result.specular = solid(name + "_s", { 255, 255, 255, 255 }, YtdImageSemantic::LinearData);
	if (input.metallicRoughness)
	{
		validateImage(*input.metallicRoughness, YtdImageSemantic::LinearData);
		result.specular = *input.metallicRoughness;
		result.specular.name = name + "_s";
	}
	for (size_t p = 0; p < result.specular.rgba.size(); p += 4)
	{
		const float m = result.specular.rgba[p + 2] / 255.0f * input.metallicFactor;
		const float r = result.specular.rgba[p + 1] / 255.0f * input.roughnessFactor;
		const auto value = static_cast<uint8_t>((0.04f + 0.96f * m) * (1 - 0.75f * r) * 255 + 0.5f);
		result.specular.rgba[p] = result.specular.rgba[p + 1] = result.specular.rgba[p + 2] = value;
		result.specular.rgba[p + 3] = 255;
	}
	return result;
}
