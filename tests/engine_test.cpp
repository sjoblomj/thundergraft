// Tests for the miniaudio engine, on miniaudio's null output device, which consumes audio in real time without playing it. Platform-independent.

#include <doctest.h>

#include "MiniaudioEngine.h"

#include "fakes/TestAudioFiles.h"

#include <chrono>
#include <functional>
#include <thread>

namespace
{
	struct ExpectedSound
	{
		const char *lpszFile;
		unsigned nChannels;
		unsigned nSampleRate;
		uint64_t nFrames;
	};

	// Every fixture is half a second long
	const ExpectedSound PLAYABLE_FIXTURES[] =
	{
		{ "wav_s16_44100_stereo.wav", 2, 44100, 22050 },
		{ "wav_u8_22050_mono.wav", 1, 22050, 11025 },
		{ "wav_s24_48000_stereo.wav", 2, 48000, 24000 },
		{ "mp3_cbr_44100_stereo.mp3", 2, 44100, 22050 },
		{ "mp3_vbr_22050_mono.mp3", 1, 22050, 11025 },
		{ "mp3_id3v2_120k.mp3", 2, 44100, 22050 },
		{ "mp3_id3v1_trailer.mp3", 2, 44100, 22050 },
		{ "ogg_vorbis_44100_stereo.ogg", 2, 44100, 22050 },
		{ "ogg_vorbis_48000_mono.ogg", 1, 48000, 24000 },
		{ "flac_16_44100_stereo.flac", 2, 44100, 22050 },
		{ "flac_24_96000_stereo.flac", 2, 96000, 48000 },
	};

	// One engine for all the tests, as the plugin has
	IAudioEngine &Engine()
	{
		static std::unique_ptr<IAudioEngine> lpEngine(CreateMiniaudioEngine(MINIAUDIO_OUTPUT_NULL));
		REQUIRE(lpEngine.get());
		return *lpEngine;
	}

	// Waits up to nTimeoutMs for a condition
	bool WaitFor(std::function<bool()> condition, int nTimeoutMs = 5000)
	{
		std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now() + std::chrono::milliseconds(nTimeoutMs);
		while (std::chrono::steady_clock::now() < end)
		{
			if (condition())
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return condition();
	}

	uint64_t Cursor(IAudioSound &sound)
	{
		uint64_t nCursor = UINT64_MAX;
		CHECK(sound.GetCursor(&nCursor));
		return nCursor;
	}
}

TEST_CASE("The engine opens every supported format, with the right format and exact length")
{
	for (const ExpectedSound &expected : PLAYABLE_FIXTURES)
	{
		std::string name = expected.lpszFile;
		CAPTURE(name);

		std::unique_ptr<IAudioSound> lpSound = Engine().OpenSound(OpenFixture(expected.lpszFile), false);
		REQUIRE(lpSound.get());

		CHECK(lpSound->GetChannels() == expected.nChannels);
		CHECK(lpSound->GetSampleRate() == expected.nSampleRate);

		uint64_t nLength = 0;
		CHECK(lpSound->GetLength(&nLength));
		CHECK(nLength == expected.nFrames);

		CHECK(lpSound->IsPlaying());
		CHECK(Cursor(*lpSound) == 0);
	}
}

TEST_CASE("Files that aren't audio are rejected")
{
	for (const char *lpszName : { "not_audio.wav", "garbage.bin", "empty.bin" })
	{
		std::string name = lpszName;
		CAPTURE(name);
		std::shared_ptr<std::atomic<bool>> lpDestroyed = std::make_shared<std::atomic<bool>>(false);

		CHECK_FALSE(Engine().OpenSound(OpenFixture(lpszName, lpDestroyed), false).get());
		CHECK(*lpDestroyed);
	}

	CHECK_FALSE(Engine().OpenSound(nullptr, false).get());
}

TEST_CASE("A truncated file plays what there is of it, or is rejected")
{
	std::unique_ptr<IAudioSound> lpSound = Engine().OpenSound(OpenFixture("truncated.mp3"), false);
	if (lpSound)
	{
		REQUIRE(lpSound->Start());
		CHECK(WaitFor([&]() { return !lpSound->IsPlaying(); }));
	}
}

TEST_CASE("Sounds play to their end")
{
	for (const char *lpszName : { "wav_u8_22050_mono.wav", "mp3_cbr_44100_stereo.mp3", "ogg_vorbis_48000_mono.ogg", "flac_16_44100_stereo.flac" })
	{
		std::string name = lpszName;
		CAPTURE(name);

		std::unique_ptr<IAudioSound> lpSound = Engine().OpenSound(OpenFixture(lpszName), false);
		REQUIRE(lpSound.get());
		REQUIRE(lpSound->Start());

		CHECK(WaitFor([&]() { return Cursor(*lpSound) > 0; }));
		CHECK(lpSound->IsPlaying());

		// Half a second of audio, in real time
		CHECK(WaitFor([&]() { return !lpSound->IsPlaying(); }));

		uint64_t nLength = 0;
		REQUIRE(lpSound->GetLength(&nLength));
		CHECK(Cursor(*lpSound) == nLength);
	}
}

TEST_CASE("Looping sounds keep playing, and their cursor wraps")
{
	for (const char *lpszName : { "wav_u8_22050_mono.wav", "ogg_vorbis_48000_mono.ogg", "mp3_cbr_44100_stereo.mp3" })
	{
		std::string name = lpszName;
		CAPTURE(name);

		std::unique_ptr<IAudioSound> lpSound = Engine().OpenSound(OpenFixture(lpszName), true);
		REQUIRE(lpSound.get());
		REQUIRE(lpSound->Start());

		uint64_t nLength = 0;
		REQUIRE(lpSound->GetLength(&nLength));

		// Long enough to have looped at least once
		std::this_thread::sleep_for(std::chrono::milliseconds(800));
		CHECK(lpSound->IsPlaying());
		CHECK(Cursor(*lpSound) < nLength);
	}
}

TEST_CASE("Sounds can start part of the way in")
{
	std::unique_ptr<IAudioSound> lpSound = Engine().OpenSound(OpenFixture("wav_s16_44100_stereo.wav"), false);
	REQUIRE(lpSound.get());

	REQUIRE(lpSound->SeekToFrame(20000));
	CHECK(WaitFor([&]() { return Cursor(*lpSound) >= 20000; }, 1000));

	// Only 50 ms are left, so it ends quickly
	REQUIRE(lpSound->Start());
	CHECK(WaitFor([&]() { return !lpSound->IsPlaying(); }, 1000));
}

TEST_CASE("Volume, pan, pitch and muting can be changed while playing")
{
	std::unique_ptr<IAudioSound> lpSound = Engine().OpenSound(OpenFixture("ogg_vorbis_44100_stereo.ogg"), true);
	REQUIRE(lpSound.get());
	REQUIRE(lpSound->Start());

	lpSound->SetVolume(0.5f);
	lpSound->SetPan(-0.7f);
	lpSound->SetMuted(true);
	lpSound->SetVolume(0.25f);
	lpSound->SetMuted(false);
	lpSound->SetPitch(0.0f);	// Ignored
	lpSound->SetPitch(-1.0f);	// Ignored

	SUBCASE("A higher pitch plays faster")
	{
		lpSound->SetPitch(2.0f);
		CHECK(WaitFor([&]() { return Cursor(*lpSound) > 0; }));
		CHECK(lpSound->IsPlaying());
	}
}

TEST_CASE("Destroying a sound stops it and releases its file")
{
	std::shared_ptr<std::atomic<bool>> lpDestroyed = std::make_shared<std::atomic<bool>>(false);
	std::unique_ptr<IAudioSound> lpSound = Engine().OpenSound(OpenFixture("flac_24_96000_stereo.flac", lpDestroyed), true);
	REQUIRE(lpSound.get());
	REQUIRE(lpSound->Start());
	CHECK(WaitFor([&]() { return Cursor(*lpSound) > 0; }));

	CHECK_FALSE(*lpDestroyed);
	lpSound.reset();
	CHECK(*lpDestroyed);
}

TEST_CASE("Several sounds can play at once")
{
	std::vector<std::unique_ptr<IAudioSound>> sounds;
	for (int i = 0; i < 3; i++)
	{
		for (const ExpectedSound &expected : PLAYABLE_FIXTURES)
		{
			sounds.push_back(Engine().OpenSound(OpenFixture(expected.lpszFile), true));
			REQUIRE(sounds.back().get());
			REQUIRE(sounds.back()->Start());
		}
	}

	CHECK(WaitFor([&]() {
		for (std::unique_ptr<IAudioSound> &lpSound : sounds)
		{
			if (Cursor(*lpSound) == 0)
				return false;
		}
		return true;
	}));
}

TEST_CASE("Sounds can be opened from several threads at once")
{
	std::atomic<int> nOpened(0);
	std::vector<std::thread> threads;
	for (int iThread = 0; iThread < 4; iThread++)
	{
		threads.emplace_back([&nOpened, iThread]() {
			for (int i = 0; i < 10; i++)
			{
				const ExpectedSound &expected = PLAYABLE_FIXTURES[(iThread + i) % (sizeof(PLAYABLE_FIXTURES) / sizeof(PLAYABLE_FIXTURES[0]))];
				std::unique_ptr<IAudioSound> lpSound = Engine().OpenSound(OpenFixture(expected.lpszFile), (i % 2) == 0);
				if (lpSound && lpSound->Start())
					nOpened++;
			}
		});
	}

	for (std::thread &thread : threads)
		thread.join();

	CHECK(nOpened == 40);
}
