// Tests of CDdaPlayer playing sounds read from real MPQs through StormLib, stored the ways MPQs store files: compressed in sectors, imploded, encrypted, and so on. They play on the real engine, rendered without a device, and are measured as in dda_output_test.cpp.

#include <doctest.h>

#include "DdaPlayer.h"
#include "MiniaudioEngine.h"

#include "fakes/MpqStorm.h"
#include "fakes/OutputMeasurement.h"
#include "fakes/TestAudioFiles.h"

#include <StormLib.h>

namespace
{
	const float TONE_FREQUENCY = 440;
	const uint64_t TONE_FRAMES = MINIAUDIO_MANUAL_SAMPLE_RATE / 2;

	// One fixture of each format; the MP3's 120 KB tag puts its audio many sectors in
	const char *const FIXTURES[] = { "wav_s16_44100_stereo.wav", "mp3_id3v2_120k.mp3", "ogg_vorbis_44100_stereo.ogg", "flac_16_44100_stereo.flac" };

	struct Storage
	{
		const char *lpszName;
		DWORD dwFlags;
		DWORD dwCompression;
	};

	const Storage STORAGES[] =
	{
		{ "uncompressed", 0, 0 },
		{ "imploded", MPQ_FILE_IMPLODE, 0 },
		{ "PKWARE compressed", MPQ_FILE_COMPRESS, MPQ_COMPRESSION_PKWARE },
		{ "zlib compressed", MPQ_FILE_COMPRESS, MPQ_COMPRESSION_ZLIB },
		{ "bzip2 compressed", MPQ_FILE_COMPRESS, MPQ_COMPRESSION_BZIP2 },
		{ "encrypted", MPQ_FILE_IMPLODE | MPQ_FILE_ENCRYPTED, 0 },
		{ "encrypted with the key adjusted by position", MPQ_FILE_COMPRESS | MPQ_FILE_ENCRYPTED | MPQ_FILE_KEY_V2, MPQ_COMPRESSION_ZLIB },
		{ "in a single unit", MPQ_FILE_COMPRESS | MPQ_FILE_SINGLE_UNIT, MPQ_COMPRESSION_ZLIB },
		{ "with sector checksums", MPQ_FILE_COMPRESS | MPQ_FILE_SECTOR_CRC, MPQ_COMPRESSION_ZLIB },
	};

	MpqStorm::ArchivedFile Archived(const char *lpszFixture, DWORD dwFlags, DWORD dwCompression, DWORD dwCompressionNext = MPQ_COMPRESSION_NEXT_SAME)
	{
		MpqStorm::ArchivedFile file;
		file.localPath = FixturePath(lpszFixture);
		file.archivedName = std::string("music\\") + lpszFixture;
		file.dwFlags = dwFlags;
		file.dwCompression = dwCompression;
		file.dwCompressionNext = dwCompressionNext;
		return file;
	}

	struct MpqFixture
	{
		std::string archivePath = MpqStorm::TempArchivePath("dda_player_tests");
		HANDLE hMpq = NULL;
		std::unique_ptr<IAudioEngine> lpEngine;
		CDdaPlayer player;

		MpqFixture()
		{
			lpEngine.reset(CreateMiniaudioEngine(MINIAUDIO_OUTPUT_MANUAL));
			REQUIRE(lpEngine.get());
			player.Initialize(MpqStorm::Functions(), lpEngine.get());
		}

		~MpqFixture()
		{
			player.StopAll();
			CloseArchive();
			DeleteFileA(archivePath.c_str());
		}

		void CreateArchive(const std::vector<MpqStorm::ArchivedFile> &files)
		{
			CloseArchive();
			REQUIRE(MpqStorm::CreateArchive(archivePath, files));
			hMpq = MpqStorm::OpenArchive(archivePath);
			REQUIRE(hMpq);
		}

		void CloseArchive()
		{
			if (hMpq)
				MpqStorm::CloseArchive(hMpq);
			hMpq = NULL;
		}

		HANDLE Open(const char *lpszFixture)
		{
			HANDLE hFile = MpqStorm::OpenFile(hMpq, std::string("music\\") + lpszFixture);
			REQUIRE_MESSAGE(hFile, "Couldn't open " << lpszFixture << " in the archive, error " << GetLastError());
			return hFile;
		}

		// Plays a file from the archive to its end, and measures what was heard
		OutputMeasurement PlayAndMeasure(const char *lpszFixture, DWORD *lpnLength = NULL)
		{
			HANDLE hFile = Open(lpszFixture);
			REQUIRE(player.Begin(hFile, 0, 0x10000, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, NULL));
			if (lpnLength)
				REQUIRE(player.GetPos(hFile, NULL, lpnLength));

			OutputMeasurement measurement = MeasureOutput(RenderOutput(*lpEngine));
			player.End(hFile);
			CHECK(player.CloseFile(hFile));
			return measurement;
		}

		void CheckTone(const OutputMeasurement &measurement)
		{
			REQUIRE(measurement.bHeard);
			CHECK(measurement.left.nFrequency == Within(TONE_FREQUENCY, 0.03));
			CHECK(measurement.right.nFrequency == Within(TONE_FREQUENCY, 0.03));
			CHECK(measurement.left.nRms > 0.05f);
			CHECK(measurement.nAudibleFrames == Within((double)TONE_FRAMES, 0.03));
		}
	};
}

TEST_CASE("StormLib's file functions behave as the player expects Storm's to")
{
	std::string path = MpqStorm::TempArchivePath("dda_player_tests storm");
	REQUIRE(MpqStorm::CreateArchive(path, { Archived("wav_s16_44100_stereo.wav", MPQ_FILE_COMPRESS, MPQ_COMPRESSION_ZLIB) }));
	HANDLE hMpq = MpqStorm::OpenArchive(path);
	REQUIRE(hMpq);
	HANDLE hFile = MpqStorm::OpenFile(hMpq, "music\\wav_s16_44100_stereo.wav");
	REQUIRE(hFile);

	STORMFILEFUNCTIONS storm = MpqStorm::Functions();
	DWORD nSize = storm.SFileGetFileSize(hFile, NULL);
	CHECK(nSize == ReadFixture("wav_s16_44100_stereo.wav").size());

	SUBCASE("Short reads fail with ERROR_HANDLE_EOF, but say how much was read")
	{
		char buffer[64];
		DWORD nRead = 0;
		REQUIRE(storm.SFileSetFilePointer(hFile, -10, NULL, FILE_END) == nSize - 10);
		CHECK_FALSE(storm.SFileReadFile(hFile, buffer, sizeof(buffer), &nRead, NULL));
		CHECK(GetLastError() == ERROR_HANDLE_EOF);
		CHECK(nRead == 10);
	}

	SUBCASE("Seeking past the end stops at the end; seeking before the start fails")
	{
		CHECK(storm.SFileSetFilePointer(hFile, 100, NULL, FILE_END) == nSize);
		CHECK(storm.SFileSetFilePointer(hFile, -1, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER);
	}

	SUBCASE("Handles that weren't opened fail")
	{
		HANDLE hBogus = (HANDLE)0xF11E;
		DWORD nRead;
		CHECK_FALSE(storm.SFileReadFile(hBogus, &nRead, 1, &nRead, NULL));
		CHECK(storm.SFileGetFileSize(hBogus, NULL) == INVALID_FILE_SIZE);
		CHECK_FALSE(storm.SFileCloseFile(hBogus));
	}

	CHECK(storm.SFileCloseFile(hFile));
	CHECK(MpqStorm::CloseArchive(hMpq));
	DeleteFileA(path.c_str());
}

TEST_CASE_FIXTURE(MpqFixture, "Every format plays from an MPQ, however it is stored")
{
	for (const Storage &storage : STORAGES)
	{
		std::string storageName = storage.lpszName;
		CAPTURE(storageName);

		std::vector<MpqStorm::ArchivedFile> files;
		for (const char *lpszFixture : FIXTURES)
			files.push_back(Archived(lpszFixture, storage.dwFlags, storage.dwCompression));
		CreateArchive(files);

		for (const char *lpszFixture : FIXTURES)
		{
			std::string name = lpszFixture;
			CAPTURE(name);

			DWORD nLength = 0;
			CheckTone(PlayAndMeasure(lpszFixture, &nLength));
			// Every fixture is 16-bit stereo or plays as such: half a second at 44100 hz
			CHECK(nLength == 22050 * 4);
		}
	}
}

TEST_CASE_FIXTURE(MpqFixture, "WAV files compressed with ADPCM play")
{
	// Lossy: the first sector, with the header, is compressed losslessly and the rest with ADPCM and Huffman coding
	CreateArchive({ Archived("wav_s16_44100_stereo.wav", MPQ_FILE_COMPRESS, MPQ_COMPRESSION_PKWARE, MPQ_COMPRESSION_ADPCM_STEREO | MPQ_COMPRESSION_HUFFMANN) });

	DWORD nLength = 0;
	CheckTone(PlayAndMeasure("wav_s16_44100_stereo.wav", &nLength));
	CHECK(nLength == 22050 * 4);
}

TEST_CASE_FIXTURE(MpqFixture, "A compressed file the game already read from plays from its start")
{
	CreateArchive({ Archived("ogg_vorbis_44100_stereo.ogg", MPQ_FILE_COMPRESS, MPQ_COMPRESSION_ZLIB) });

	// Into the second sector
	HANDLE hFile = Open("ogg_vorbis_44100_stereo.ogg");
	std::vector<char> buffer(5000);
	DWORD nRead = 0;
	REQUIRE(MpqStorm::Functions().SFileReadFile(hFile, &buffer[0], (DWORD)buffer.size(), &nRead, NULL));

	REQUIRE(player.Begin(hFile, 0, 0x10000, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, NULL));
	CheckTone(MeasureOutput(RenderOutput(*lpEngine)));
	player.End(hFile);
	CHECK(player.CloseFile(hFile));
}

TEST_CASE_FIXTURE(MpqFixture, "Several sounds play from one archive at once")
{
	std::vector<MpqStorm::ArchivedFile> files;
	for (const char *lpszFixture : FIXTURES)
		files.push_back(Archived(lpszFixture, MPQ_FILE_IMPLODE | MPQ_FILE_ENCRYPTED, 0));
	CreateArchive(files);

	std::vector<HANDLE> hFiles;
	for (const char *lpszFixture : FIXTURES)
	{
		hFiles.push_back(Open(lpszFixture));
		REQUIRE(player.Begin(hFiles.back(), 0, 0x10000, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, NULL));
	}

	// The same tone four times over is still a 440 hz tone. The sounds may start a little apart, as each is decoded on its own.
	OutputMeasurement measurement = MeasureOutput(RenderOutput(*lpEngine));
	REQUIRE(measurement.bHeard);
	CHECK(measurement.left.nFrequency == Within(TONE_FREQUENCY, 0.03));
	CHECK(measurement.nAudibleFrames >= TONE_FRAMES * 0.97);

	for (HANDLE hFile : hFiles)
	{
		player.End(hFile);
		CHECK(player.CloseFile(hFile));
	}
}
