#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

enum class GameGeneration { Legacy, Enhanced };

inline uint32_t assetNameHash(const std::string& name)
{
	uint32_t h = 0;
	for (unsigned char c : name)
	{
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		h += c; h += h << 10; h ^= h >> 6;
	}
	h += h << 3; h ^= h >> 11; h += h << 15;
	return h;
}

inline std::string normalizeAssetName(const std::string& name, const char* kind)
{
	if (name.empty() || name.size() > 127)
		throw std::invalid_argument(std::string(kind) + " names must contain 1..127 ASCII letters, digits or underscores.");
	std::string result;
	for (unsigned char c : name)
	{
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_')
			throw std::invalid_argument(std::string("Invalid ") + kind + " name: " + name);
		result.push_back(static_cast<char>(c));
	}
	return result;
}
