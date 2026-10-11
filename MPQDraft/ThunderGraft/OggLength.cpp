/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "OggLength.h"

#include <string.h>
#include <vector>

namespace
{
	// The CRC lookup table, built at compile time so it's safe to use from any thread
	struct OggCrcTable
	{
		uint32_t entries[256];

		constexpr OggCrcTable() : entries()
		{
			for (uint32_t iEntry = 0; iEntry < 256; iEntry++)
			{
				uint32_t nValue = iEntry << 24;
				for (int iBit = 0; iBit < 8; iBit++)
					nValue = (nValue & 0x80000000) ? (nValue << 1) ^ 0x04C11DB7 : (nValue << 1);

				entries[iEntry] = nValue;
			}
		}
	};

	constexpr OggCrcTable g_oggCrcTable;
}

uint32_t OggCrcUpdate(uint32_t nCrc, const unsigned char *lpData, size_t nBytes)
{
	// CRC-32 with polynomial 0x04C11DB7, not reflected, no final XOR
	for (size_t iByte = 0; iByte < nBytes; iByte++)
		nCrc = (nCrc << 8) ^ g_oggCrcTable.entries[((nCrc >> 24) ^ lpData[iByte]) & 0xFF];

	return nCrc;
}

uint32_t OggCrc(const unsigned char *lpData, size_t nBytes)
{
	return OggCrcUpdate(0, lpData, nBytes);
}


namespace
{
	// The layout of an Ogg page header (RFC 3533). All fields are little-endian.
	const size_t OGG_HEADER_SIZE = 27;
	const size_t OGG_VERSION_OFFSET = 4;
	const size_t OGG_GRANULE_OFFSET = 6;
	const size_t OGG_SERIAL_OFFSET = 14;
	const size_t OGG_CRC_OFFSET = 22;
	const size_t OGG_SEGMENT_COUNT_OFFSET = 26;

	// Pages on which no packet ends have this granule position
	const uint64_t OGG_NO_GRANULE = UINT64_MAX;

	uint32_t ReadLE32(const unsigned char *lpData)
	{
		return (uint32_t)lpData[0] | ((uint32_t)lpData[1] << 8) | ((uint32_t)lpData[2] << 16) | ((uint32_t)lpData[3] << 24);
	}

	uint64_t ReadLE64(const unsigned char *lpData)
	{
		return (uint64_t)ReadLE32(lpData) | ((uint64_t)ReadLE32(lpData + 4) << 32);
	}

	bool ReadFully(IAudioFile &file, void *lpBuffer, size_t nBytes, size_t *lpnBytesRead)
	{
		*lpnBytesRead = 0;
		while (*lpnBytesRead < nBytes)
		{
			size_t nRead = 0;
			if (!file.Read((unsigned char *)lpBuffer + *lpnBytesRead, nBytes - *lpnBytesRead, &nRead))
				return false;
			if (!nRead)
				break;

			*lpnBytesRead += nRead;
		}

		return true;
	}

	// If a complete, valid page starts at nOffset in the buffer, returns its total size, otherwise 0
	size_t CheckPage(const std::vector<unsigned char> &buffer, size_t nOffset)
	{
		if (nOffset + OGG_HEADER_SIZE > buffer.size())
			return 0;

		const unsigned char *lpPage = &buffer[nOffset];
		if (memcmp(lpPage, "OggS", 4) != 0 || lpPage[OGG_VERSION_OFFSET] != 0)
			return 0;

		size_t nSegments = lpPage[OGG_SEGMENT_COUNT_OFFSET];
		size_t nHeaderSize = OGG_HEADER_SIZE + nSegments;
		if (nOffset + nHeaderSize > buffer.size())
			return 0;

		size_t nPageSize = nHeaderSize;
		for (size_t iSegment = 0; iSegment < nSegments; iSegment++)
			nPageSize += lpPage[OGG_HEADER_SIZE + iSegment];
		if (nOffset + nPageSize > buffer.size())
			return 0;

		// "OggS" may well turn up in the middle of compressed audio, so only trust pages whose CRC matches. The CRC is of the page with the CRC field itself zeroed.
		static const unsigned char zeroes[4] = { 0, 0, 0, 0 };
		uint32_t nCrc = OggCrcUpdate(0, lpPage, OGG_CRC_OFFSET);
		nCrc = OggCrcUpdate(nCrc, zeroes, sizeof(zeroes));
		nCrc = OggCrcUpdate(nCrc, lpPage + OGG_CRC_OFFSET + 4, nPageSize - OGG_CRC_OFFSET - 4);
		if (nCrc != ReadLE32(lpPage + OGG_CRC_OFFSET))
			return 0;

		return nPageSize;
	}

	// Reads the first page and checks that it starts a Vorbis stream. Returns the stream's serial number.
	bool ReadFirstPage(IAudioFile &file, uint32_t *lpnSerial)
	{
		if (!file.Seek(0, AUDIO_SEEK_BEGIN))
			return false;

		// The first page of a Vorbis stream holds only the identification header, which is small
		std::vector<unsigned char> buffer(OGG_HEADER_SIZE + 255 + 255);
		size_t nRead;
		if (!ReadFully(file, &buffer[0], buffer.size(), &nRead))
			return false;
		buffer.resize(nRead);

		size_t nPageSize = CheckPage(buffer, 0);
		if (!nPageSize)
			return false;

		// The identification header packet starts right after the segment table
		size_t nPacketOffset = OGG_HEADER_SIZE + buffer[OGG_SEGMENT_COUNT_OFFSET];
		if (nPacketOffset + 7 > nPageSize || memcmp(&buffer[nPacketOffset], "\x01vorbis", 7) != 0)
			return false;

		*lpnSerial = ReadLE32(&buffer[OGG_SERIAL_OFFSET]);

		return true;
	}
}

bool IsOggVorbis(IAudioFile &file)
{
	uint32_t nSerial;
	return ReadFirstPage(file, &nSerial);
}

bool GetOggVorbisLength(IAudioFile &file, uint64_t *lpnFrames)
{
	uint32_t nSerial;
	if (!ReadFirstPage(file, &nSerial))
		return false;

	// Read the end of the file
	uint64_t nFileSize;
	if (!file.GetSize(&nFileSize))
		return false;

	size_t nSearchBytes = (size_t)(nFileSize < OGG_LENGTH_SEARCH_BYTES ? nFileSize : OGG_LENGTH_SEARCH_BYTES);
	if (!file.Seek((int64_t)(nFileSize - nSearchBytes), AUDIO_SEEK_BEGIN))
		return false;

	std::vector<unsigned char> buffer(nSearchBytes);
	size_t nRead;
	if (!nSearchBytes || !ReadFully(file, &buffer[0], buffer.size(), &nRead))
		return false;
	buffer.resize(nRead);

	// Look for the last complete page of our stream that has a granule position. Search backwards, so the first one found is the last.
	for (size_t nOffset = buffer.size(); nOffset-- > 0; )
	{
		if (buffer[nOffset] != 'O' || !CheckPage(buffer, nOffset))
			continue;

		const unsigned char *lpPage = &buffer[nOffset];
		uint64_t nGranule = ReadLE64(lpPage + OGG_GRANULE_OFFSET);
		if (ReadLE32(lpPage + OGG_SERIAL_OFFSET) != nSerial || nGranule == OGG_NO_GRANULE)
			continue;

		*lpnFrames = nGranule;

		return true;
	}

	return false;
}
