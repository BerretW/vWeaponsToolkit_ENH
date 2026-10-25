#pragma once
#include "cAssetFormat.h"
#include <cstdint>
#include <string>
#include <vector>

enum class YtdImageSemantic
{
	ColorSrgb,
	LinearData,
	TangentNormal
};

struct YtdImage
{
	std::string name;
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> rgba;
	YtdImageSemantic semantic = YtdImageSemantic::ColorSrgb;
};

// Color uses alpha-weighted linear-light filtering; data uses unweighted linear
// filtering; tangent normals use linear vector filtering and renormalization.
// Returns one half-resolution level (clamped to 1x1), without BC compression.
YtdImage downsampleYtdImage(const YtdImage& image);

// 2D textures, power-of-two dimensions, max 4096. Throws on invalid input.
// Legacy BC textures require at least 4x4 and stop mips when either side reaches 4.
// Enhanced supports 1x1 and a full mip chain. Opaque = BC1, alpha = BC3.
std::vector<uint8_t> buildYtd(const std::vector<YtdImage>& images, GameGeneration generation);
