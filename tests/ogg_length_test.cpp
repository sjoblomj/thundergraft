// Tests for reading the length of Ogg Vorbis files from their last page. Platform-independent.

#include <doctest.h>

#include "OggLength.h"

#include "fakes/TestAudioFiles.h"

namespace
{
	uint64_t OggLength(const std::vector<unsigned char> &contents, bool *lpbFound)
	{
		MemoryAudioFile file(contents);
		uint64_t nFrames = 0;
		*lpbFound = GetOggVorbisLength(file, &nFrames);
		return nFrames;
	}

	void WriteLE(std::vector<unsigned char> &buffer, size_t nOffset, uint64_t nValue, int nBytes)
	{
		for (int iByte = 0; iByte < nBytes; iByte++)
			buffer[nOffset + iByte] = (unsigned char)(nValue >> (8 * iByte));
	}

	// Builds a valid Ogg page holding one packet
	std::vector<unsigned char> MakePage(uint32_t nSerial, uint64_t nGranule, uint32_t nSequence, const std::vector<unsigned char> &packet)
	{
		REQUIRE(packet.size() < 255);

		std::vector<unsigned char> page(28 + packet.size());
		memcpy(&page[0], "OggS", 4);
		WriteLE(page, 6, nGranule, 8);
		WriteLE(page, 14, nSerial, 4);
		WriteLE(page, 18, nSequence, 4);
		page[26] = 1;
		page[27] = (unsigned char)packet.size();
		if (!packet.empty())
			memcpy(&page[28], &packet[0], packet.size());

		WriteLE(page, 22, OggCrc(&page[0], page.size()), 4);
		return page;
	}

	uint32_t SerialOf(const std::vector<unsigned char> &contents)
	{
		return contents[14] | (contents[15] << 8) | (contents[16] << 16) | ((uint32_t)contents[17] << 24);
	}
}

TEST_CASE("The CRC of a real Ogg page matches the one stored in it")
{
	std::vector<unsigned char> contents = ReadFixture("ogg_vorbis_44100_stereo.ogg");
	REQUIRE(contents.size() > 58);

	// The first page of a Vorbis stream is the 30-byte identification header in one segment
	std::vector<unsigned char> page(contents.begin(), contents.begin() + 28 + contents[27]);
	uint32_t nStored = page[22] | (page[23] << 8) | (page[24] << 16) | ((uint32_t)page[25] << 24);
	memset(&page[22], 0, 4);
	CHECK(OggCrc(&page[0], page.size()) == nStored);

	CHECK(OggCrc(NULL, 0) == 0);
}

TEST_CASE("The length of Ogg Vorbis files comes from their last page")
{
	bool bFound;
	CHECK(OggLength(ReadFixture("ogg_vorbis_44100_stereo.ogg"), &bFound) == 22050);
	CHECK(bFound);
	CHECK(OggLength(ReadFixture("ogg_vorbis_48000_mono.ogg"), &bFound) == 24000);
	CHECK(bFound);
}

TEST_CASE("Files that aren't Ogg Vorbis have no Ogg length")
{
	for (const char *lpszName : { "wav_s16_44100_stereo.wav", "mp3_cbr_44100_stereo.mp3", "flac_16_44100_stereo.flac", "not_audio.wav", "garbage.bin", "empty.bin" })
	{
		std::string name = lpszName;
		CAPTURE(name);
		bool bFound = true;
		OggLength(ReadFixture(lpszName), &bFound);
		CHECK_FALSE(bFound);

		MemoryAudioFile file(ReadFixture(lpszName));
		CHECK_FALSE(IsOggVorbis(file));
	}

	SUBCASE("Ogg files that aren't Vorbis")
	{
		std::vector<unsigned char> packet = { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 2, 0, 0, 0x80, 0xBB, 0, 0, 0, 0, 0 };
		bool bFound = true;
		OggLength(MakePage(1, 0, 0, packet), &bFound);
		CHECK_FALSE(bFound);
	}
}

TEST_CASE("Ogg Vorbis files are recognized")
{
	MemoryAudioFile file(ReadFixture("ogg_vorbis_44100_stereo.ogg"));
	CHECK(IsOggVorbis(file));
}

TEST_CASE("Damaged and unusual endings")
{
	std::vector<unsigned char> contents = ReadFixture("ogg_vorbis_44100_stereo.ogg");
	REQUIRE_FALSE(contents.empty());
	uint32_t nSerial = SerialOf(contents);
	bool bFound;

	SUBCASE("A cut-off last page is skipped in favour of the one before")
	{
		contents.resize(contents.size() - 10);
		uint64_t nLength = OggLength(contents, &bFound);
		REQUIRE(bFound);
		CHECK(nLength < 22050);
	}

	SUBCASE("A last page with a bad CRC is skipped")
	{
		contents[contents.size() - 1] ^= 0xFF;
		uint64_t nLength = OggLength(contents, &bFound);
		REQUIRE(bFound);
		CHECK(nLength < 22050);
	}

	SUBCASE("Pages of another stream are ignored")
	{
		std::vector<unsigned char> other = MakePage(nSerial + 1, 999999, 0, { 1, 2, 3 });
		contents.insert(contents.end(), other.begin(), other.end());
		CHECK(OggLength(contents, &bFound) == 22050);
		CHECK(bFound);
	}

	SUBCASE("Pages where no packet ends are ignored")
	{
		std::vector<unsigned char> continuation = MakePage(nSerial, UINT64_MAX, 1000, { 1, 2, 3 });
		contents.insert(contents.end(), continuation.begin(), continuation.end());
		CHECK(OggLength(contents, &bFound) == 22050);
		CHECK(bFound);
	}

	SUBCASE("\"OggS\" in the middle of other data isn't mistaken for a page")
	{
		std::vector<unsigned char> fake = { 'O', 'g', 'g', 'S', 0, 0, 0xFF, 0xFF, 0xFF, 0x00, 0, 0, 0, 0 };
		contents.insert(contents.end(), fake.begin(), fake.end());
		contents.resize(contents.size() + 40, 0);
		CHECK(OggLength(contents, &bFound) == 22050);
		CHECK(bFound);
	}

	SUBCASE("The last page is only looked for near the end of the file")
	{
		// Only the identification page, followed by more than the search window of something else
		std::vector<unsigned char> firstPage(contents.begin(), contents.begin() + 28 + contents[27]);
		firstPage.resize(firstPage.size() + OGG_LENGTH_SEARCH_BYTES + 1000, 0x55);
		OggLength(firstPage, &bFound);
		CHECK_FALSE(bFound);
	}

	SUBCASE("A file that is only the identification page has length 0")
	{
		std::vector<unsigned char> firstPage(contents.begin(), contents.begin() + 28 + contents[27]);
		CHECK(OggLength(firstPage, &bFound) == 0);
		CHECK(bFound);
	}
}
