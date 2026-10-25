#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <wx/string.h>

enum class ResourceSegment { System, Graphics };

class cRscWriter
{
public:
	uint64_t allocate(ResourceSegment segment, size_t size, size_t alignment = 16);
	void bytes(uint64_t address, const void* data, size_t size);
	void u8(uint64_t address, uint8_t value);
	void u16(uint64_t address, uint16_t value);
	void u32(uint64_t address, uint32_t value);
	void u64(uint64_t address, uint64_t value);

	// Root block must be the first system allocation and contain ResourceFileBase.
	std::vector<uint8_t> build(uint8_t version) const;
	static void save(const wxString& path, const std::vector<uint8_t>& data);

private:
	std::vector<uint8_t> system;
	std::vector<uint8_t> graphics;
};
