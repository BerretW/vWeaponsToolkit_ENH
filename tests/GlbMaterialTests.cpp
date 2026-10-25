#include "cGlbMaterials.h"
#include "third_party/cgltf.h"
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace
{
	void require(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	void rejects(const std::function<void()>& call)
	{
		try { call(); }
		catch (const std::invalid_argument&) { return; }
		throw std::runtime_error("Expected unsupported/invalid material input to be rejected.");
	}

	cgltf_material sourceMaterial()
	{
		cgltf_material source{};
		source.pbr_metallic_roughness.metallic_factor = 1;
		source.pbr_metallic_roughness.roughness_factor = 1;
		source.normal_texture.scale = 1;
		source.occlusion_texture.scale = 1;
		for (float& c : source.pbr_metallic_roughness.base_color_factor) c = 1;
		source.alpha_mode = cgltf_alpha_mode_opaque;
		return source;
	}
}

// Invoked by the existing test runner; intentionally no standalone main.
void runGlbMaterialTests()
{
	GlbMaterialInput input;
	input.name = "Weapon";
	input.baseColorFactor = { 0.5f, 1, 0, 0 };
	const auto solid = convertGlbMaterial(input);
	require(solid.diffuse.name == "weapon_d" && solid.normal.name == "weapon_n" &&
		solid.specular.name == "weapon_s", "Material texture names were not normalized.");
	require(solid.diffuse.rgba[0] == 188 && solid.diffuse.rgba[1] == 255 &&
		solid.diffuse.rgba[2] == 0 && solid.diffuse.rgba[3] == 255,
		"Linear base-color factor bake or explicit OPAQUE alpha handling failed.");
	require(solid.normal.semantic == YtdImageSemantic::TangentNormal &&
		solid.specular.semantic == YtdImageSemantic::LinearData, "Neutral map semantics were lost.");
	require(solid.normal.rgba[0] == 128 && solid.normal.rgba[1] == 128 &&
		solid.normal.rgba[2] == 255 && solid.normal.rgba[3] == 255 &&
		solid.specular.rgba[0] == 64 && solid.specular.rgba[3] == 255,
		"Neutral map values are incorrect.");
	for (const auto generation : { GameGeneration::Legacy, GameGeneration::Enhanced })
		require(!buildYtd({ solid.diffuse, solid.normal, solid.specular }, generation).empty(),
			"Converted maps did not serialize through the existing BC writer.");

	YtdImage image{ "source", 2, 2, {
		128, 64, 32, 0, 128, 64, 32, 128,
		128, 64, 32, 255, 128, 64, 32, 255 } };
	input.baseColor = &image;
	const auto baked = convertGlbMaterial(input);
	require(baked.diffuse.rgba[0] == 92 && baked.diffuse.rgba[1] == 64 &&
		baked.diffuse.rgba[2] == 0 && baked.diffuse.rgba[3] == 255,
		"Textured factor bake must operate in linear light and discard OPAQUE alpha.");
	require(image.rgba[3] == 0 && image.name == "source", "Conversion mutated source pixels.");
	for (const auto alpha : { GlbMaterialAlpha::Mask, GlbMaterialAlpha::Blend })
	{
		input.alpha = alpha;
		rejects([&] { convertGlbMaterial(input); });
	}
	input.alpha = GlbMaterialAlpha::Opaque;
	input.baseColorFactor[0] = std::numeric_limits<float>::quiet_NaN();
	rejects([&] { convertGlbMaterial(input); });
	input.baseColorFactor[0] = 1.01f;
	rejects([&] { convertGlbMaterial(input); });
	input.baseColorFactor[0] = 1;
	image.semantic = YtdImageSemantic::LinearData;
	rejects([&] { convertGlbMaterial(input); });
	image.semantic = YtdImageSemantic::ColorSrgb;
	image.width = 3;
	rejects([&] { convertGlbMaterial(input); });
	image.width = 2;
	image.rgba.pop_back();
	rejects([&] { convertGlbMaterial(input); });

	YtdImage filter{ "filter", 2, 2, {
		0, 0, 0, 255, 255, 255, 255, 255,
		0, 0, 0, 255, 255, 255, 255, 255 } };
	require(downsampleYtdImage(filter).rgba[0] == 188, "sRGB color mips changed.");
	filter.semantic = YtdImageSemantic::LinearData;
	require(downsampleYtdImage(filter).rgba[0] == 128, "Data mips incorrectly used sRGB.");
	filter.rgba[3] = 0;
	filter.rgba[11] = 0;
	const auto dataMip = downsampleYtdImage(filter);
	require(dataMip.rgba[0] == 128 && dataMip.rgba[3] == 128,
		"Data mips incorrectly alpha-weighted channels.");
	filter.semantic = YtdImageSemantic::ColorSrgb;
	require(downsampleYtdImage(filter).rgba[0] == 255, "Color mips no longer alpha-weighted.");
	filter.semantic = YtdImageSemantic::TangentNormal;
	const auto cancelled = downsampleYtdImage(filter);
	require(cancelled.rgba[0] == 128 && cancelled.rgba[1] == 128 && cancelled.rgba[2] == 255,
		"Cancelled tangent normals must use the neutral normal.");
	filter.rgba = {255,128,128,255, 128,128,255,0, 255,128,128,255, 128,128,255,0};
	const auto normalMip = downsampleYtdImage(filter);
	const float x = normalMip.rgba[0] / 127.5f - 1;
	const float y = normalMip.rgba[1] / 127.5f - 1;
	const float z = normalMip.rgba[2] / 127.5f - 1;
	require(std::fabs(std::sqrt(x*x + y*y + z*z) - 1) < 0.012f &&
		std::abs(int(normalMip.rgba[0]) - int(normalMip.rgba[2])) <= 1 &&
		normalMip.rgba[0] >= 217, "Normal mips must linearly average and renormalize, ignoring alpha.");
	filter.semantic = static_cast<YtdImageSemantic>(999);
	rejects([&] { downsampleYtdImage(filter); });
	rejects([&] { buildYtd({filter}, GameGeneration::Enhanced); });

	auto source = sourceMaterial();
	auto metadata = glbMaterialInput(source, nullptr);
	require(metadata.baseColorFactor[0] == 1, "Source material factors were not retained.");
	source.alpha_mode = cgltf_alpha_mode_mask;
	rejects([&] { glbMaterialInput(source, nullptr); });
	source = sourceMaterial();
	source.pbr_metallic_roughness.metallic_factor = 0.5f;
	source.pbr_metallic_roughness.roughness_factor = 0.25f;
	metadata = glbMaterialInput(source, nullptr);
	require(metadata.metallicFactor == 0.5f && metadata.roughnessFactor == 0.25f,
		"Metallic-roughness factors were not preserved.");
	require(convertGlbMaterial(metadata).specular.rgba[0] == 108,
		"Absent MR maps must still apply uniform metallic-roughness factors.");
	source = sourceMaterial();
	source.has_clearcoat = true;
	rejects([&] { glbMaterialInput(source, nullptr); });
	source = sourceMaterial();
	cgltf_texture texture{};
	source.normal_texture.texture = &texture;
	rejects([&] { glbMaterialInput(source, nullptr); });
	source = sourceMaterial();
	source.pbr_metallic_roughness.base_color_texture.texture = &texture;
	rejects([&] { glbMaterialInput(source, nullptr); });
	source = sourceMaterial();
	source.double_sided = true;
	rejects([&] { glbMaterialInput(source, nullptr); });

	YtdImage normalImage{ "normal", 2, 1, {255,128,255,0, 128,255,255,255},
		YtdImageSemantic::TangentNormal };
	YtdImage mrImage{ "mr", 1, 2, {0,0,255,0, 255,255,0,255}, YtdImageSemantic::LinearData };
	source = sourceMaterial();
	source.normal_texture.texture = &texture;
	source.normal_texture.scale = 2;
	source.pbr_metallic_roughness.metallic_roughness_texture.texture = &texture;
	source.pbr_metallic_roughness.metallic_factor = 0.5f;
	source.pbr_metallic_roughness.roughness_factor = 0.25f;
	metadata = glbMaterialInput(source, nullptr, &normalImage, &mrImage);
	require(metadata.normal == &normalImage && metadata.metallicRoughness == &mrImage &&
		metadata.normalScale == 2, "Source texture roles or normal scale were lost.");
	const auto mapped = convertGlbMaterial(metadata);
	require(mapped.diffuse.width == 4 && mapped.diffuse.height == 4 &&
		mapped.normal.width == 2 && mapped.normal.height == 1 &&
		mapped.specular.width == 1 && mapped.specular.height == 2,
		"Maps with different resolutions must preserve independent dimensions.");
	require(mapped.normal.rgba[0] == 242 && mapped.normal.rgba[2] == 185 &&
		mapped.normal.rgba[3] == 255 && mapped.normal.rgba[5] > 128,
		"Normal scale, renormalization, alpha or glTF green orientation was not preserved.");
	require(mapped.specular.rgba[0] == 133 && mapped.specular.rgba[1] == 133 &&
		mapped.specular.rgba[2] == 133 && mapped.specular.rgba[3] == 255 &&
		mapped.specular.rgba[4] == 8, "Artistic linear MR-to-specular formula changed.");
	require(normalImage.rgba[3] == 0 && mrImage.rgba[0] == 0 &&
		mrImage.rgba[2] == 255, "Map conversion mutated source images.");
	require(!buildYtd({mapped.diffuse, mapped.normal, mapped.specular}, GameGeneration::Enhanced).empty(),
		"Independent map dimensions did not serialize.");
	mrImage.rgba[0] = 255;
	mrImage.rgba[3] = 255;
	require(convertGlbMaterial(metadata).specular.rgba == mapped.specular.rgba,
		"MR red/alpha were incorrectly interpreted as occlusion or opacity.");
	metadata.normalScale = 0;
	const auto flat = convertGlbMaterial(metadata);
	require(flat.normal.rgba[0] == 128 && flat.normal.rgba[1] == 128 && flat.normal.rgba[2] == 255,
		"Zero normal scale should flatten positive-Z normal texels.");
	metadata.normalScale = -2;
	require(convertGlbMaterial(metadata).normal.rgba[0] == 13,
		"Finite negative glTF normal scale must invert XY.");
	metadata.normalScale = std::numeric_limits<float>::infinity();
	rejects([&] { convertGlbMaterial(metadata); });
	metadata.normalScale = 1;
	metadata.metallicFactor = -0.1f;
	rejects([&] { convertGlbMaterial(metadata); });
	metadata.metallicFactor = 1;
	metadata.roughnessFactor = std::numeric_limits<float>::quiet_NaN();
	rejects([&] { convertGlbMaterial(metadata); });
	metadata.roughnessFactor = 1;
	normalImage.semantic = YtdImageSemantic::ColorSrgb;
	rejects([&] { convertGlbMaterial(metadata); });
	rejects([&] { glbMaterialInput(source, nullptr, &normalImage, &mrImage); });
	normalImage.semantic = YtdImageSemantic::TangentNormal;
	mrImage.semantic = YtdImageSemantic::ColorSrgb;
	rejects([&] { glbMaterialInput(source, nullptr, &normalImage, &mrImage); });
	mrImage.semantic = YtdImageSemantic::LinearData;
	source.normal_texture.texcoord = 1;
	rejects([&] { glbMaterialInput(source, nullptr, &normalImage, &mrImage); });
	source.normal_texture.texcoord = 0;
	source.pbr_metallic_roughness.metallic_roughness_texture.has_transform = true;
	rejects([&] { glbMaterialInput(source, nullptr, &normalImage, &mrImage); });
	source.pbr_metallic_roughness.metallic_roughness_texture.has_transform = false;
	cgltf_sampler sampler{};
	sampler.wrap_s = cgltf_wrap_mode_clamp_to_edge;
	sampler.wrap_t = cgltf_wrap_mode_repeat;
	texture.sampler = &sampler;
	rejects([&] { glbMaterialInput(source, nullptr, &normalImage, &mrImage); });
	texture.sampler = nullptr;
	source.occlusion_texture.texture = &texture;
	source.occlusion_texture.scale = 0.5f;
	mrImage.rgba[0] = 0;
	auto aoInput = glbMaterialInput(source, nullptr, &normalImage, &mrImage);
	require(aoInput.bakeSharedOcclusion && aoInput.occlusionStrength == 0.5f,
		"Supplied GLB shared occlusion metadata was not retained.");
	const auto aoOutput = convertGlbMaterial(aoInput);
	require(aoOutput.diffuse.rgba[0] == 188 && aoOutput.diffuse.rgba[4*4*2] == 255,
		"Shared occlusion must bake in linear light using explicit nearest texel-center resampling.");
	require(aoOutput.specular.rgba == mapped.specular.rgba,
		"Shared occlusion must not alter the MR specular approximation.");
	cgltf_texture separateOcclusion{};
	source.occlusion_texture.texture = &separateOcclusion;
	rejects([&] { glbMaterialInput(source, nullptr, &normalImage, &mrImage); });
	source.occlusion_texture.texture = nullptr;
	source.alpha_mode = cgltf_alpha_mode_blend;
	rejects([&] { glbMaterialInput(source, nullptr, &normalImage, &mrImage); });
}
