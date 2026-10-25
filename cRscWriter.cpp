// SPDX-License-Identifier: GPL-3.0-or-later
// Original single-page writer for the RSC7 binary layout. See THIRD_PARTY_NOTICES.txt.
#include <wx/wx.h>
#include "cRscWriter.h"
#include <wx/file.h>
#include <wx/mstream.h>
#include <wx/zstream.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace
{
	const size_t maxSegmentSize = 128 * 1024 * 1024;

	struct Page
	{
		size_t size;
		uint32_t flags;
	};

	Page pageFor(size_t size)
	{
		if (!size) return { 0, 0 };
		size_t capacity = 8192;
		unsigned shift = 4;
		unsigned bit = 27;
		while (capacity < size)
		{
			capacity *= 2;
			if (shift < 15) ++shift;
			else --bit;
		}
		return { capacity, (uint32_t(1) << bit) | shift };
	}
}

uint64_t cRscWriter::allocate(ResourceSegment segment, size_t size, size_t alignment)
{
	if (segment != ResourceSegment::System && segment != ResourceSegment::Graphics)
		throw std::invalid_argument("Invalid resource segment.");
	if (!size || !alignment || (alignment & (alignment - 1)) || alignment > maxSegmentSize)
		throw std::invalid_argument("Invalid resource allocation size/alignment.");
	auto& buffer = segment == ResourceSegment::System ? system : graphics;
	size_t offset = (buffer.size() + alignment - 1) & ~(alignment - 1);
	if (size > maxSegmentSize || offset > maxSegmentSize - size)
		throw std::length_error("Resource segment exceeds the 128 MiB writer limit.");
	buffer.resize(offset + size, 0);
	return (segment == ResourceSegment::System ? 0x50000000ull : 0x60000000ull) + offset;
}

void cRscWriter::bytes(uint64_t address, const void* data, size_t size)
{
	const uint64_t tag = address & ~0x0FFFFFFFull;
	if (tag != 0x50000000 && tag != 0x60000000)
		throw std::invalid_argument("Invalid resource pointer.");
	auto& buffer = tag == 0x50000000 ? system : graphics;
	size_t offset = static_cast<size_t>(address & 0x0FFFFFFF);
	if (offset > buffer.size() || size > buffer.size() - offset || (!data && size))
		throw std::out_of_range("Resource write outside allocated storage.");
	if (size) std::memcpy(buffer.data() + offset, data, size);
}

void cRscWriter::u8(uint64_t address, uint8_t value) { bytes(address, &value, 1); }
void cRscWriter::u16(uint64_t address, uint16_t value)
{
	uint8_t b[2] = { static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8) };
	bytes(address, b, sizeof(b));
}
void cRscWriter::u32(uint64_t address, uint32_t value)
{
	uint8_t b[4];
	for (unsigned i = 0; i < 4; ++i) b[i] = static_cast<uint8_t>(value >> (8 * i));
	bytes(address, b, sizeof(b));
}
void cRscWriter::u64(uint64_t address, uint64_t value)
{
	uint8_t b[8];
	for (unsigned i = 0; i < 8; ++i) b[i] = static_cast<uint8_t>(value >> (8 * i));
	bytes(address, b, sizeof(b));
}

std::vector<uint8_t> cRscWriter::build(uint8_t version) const
{
	if (system.size() < 16)
		throw std::logic_error("ResourceFileBase must be allocated first.");
	const Page gfx = pageFor(graphics.size());

	// Both segments use one page: no block crosses a page boundary.
	// Append the runtime page table after user blocks; its pointer lives in ResourceFileBase.
	const size_t tableOffset = (system.size() + 15) & ~size_t(15);
	const size_t tableSize = 16 + 8 * (1 + (gfx.size ? 1 : 0));
	const Page finalSys = pageFor(tableOffset + tableSize);
	if (finalSys.size > maxSegmentSize)
		throw std::length_error("Resource page table exceeds the system segment limit.");
	std::vector<uint8_t> payload(finalSys.size + gfx.size, 0);
	std::copy(system.begin(), system.end(), payload.begin());
	std::copy(graphics.begin(), graphics.end(), payload.begin() + finalSys.size);
	const uint64_t tablePointer = 0x50000000ull + tableOffset;
	for (unsigned i = 0; i < 8; ++i) payload[8 + i] = static_cast<uint8_t>(tablePointer >> (8 * i));
	payload[tableOffset + 8] = 1;
	payload[tableOffset + 9] = gfx.size ? 1 : 0;

	wxMemoryOutputStream compressed;
	{
		wxZlibOutputStream deflate(compressed, -1, wxZLIB_NO_HEADER);
		deflate.Write(payload.data(), payload.size());
		if (!deflate.IsOk() || !deflate.Close())
			throw std::runtime_error("RSC7 compression failed.");
	}
	std::vector<uint8_t> result(16 + compressed.GetSize());
	const uint32_t header[4] = { 0x37435352, version,
		finalSys.flags | (uint32_t(version >> 4) << 28),
		gfx.flags | (uint32_t(version & 15) << 28) };
	for (unsigned i = 0; i < 4; ++i)
		for (unsigned j = 0; j < 4; ++j)
			result[i * 4 + j] = static_cast<uint8_t>(header[i] >> (8 * j));
	compressed.CopyTo(result.data() + 16, result.size() - 16);
	return result;
}

void cRscWriter::save(const wxString& path, const std::vector<uint8_t>& data)
{
	if (data.empty()) throw std::invalid_argument("Cannot save an empty resource.");
	wxTempFile file(path);
	if (!file.IsOpened() || !file.Write(data.data(), data.size()) || !file.Commit())
		throw std::runtime_error("Could not save resource: " + path.ToStdString());
}
