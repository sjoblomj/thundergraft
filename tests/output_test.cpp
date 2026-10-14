// Tests of what the miniaudio engine would actually send to the speakers, rendered without a device. The fixtures are all half a second of a 440 hz tone, so the level and frequency of each output channel show whether volume, pan, pitch and resampling were applied. Platform-independent.

#include <doctest.h>

#include "MiniaudioEngine.h"

#include "fakes/OutputMeasurement.h"
#include "fakes/TestAudioFiles.h"

#include <math.h>
#include <chrono>
#include <string>
#include <thread>

namespace
{
	const float TONE_FREQUENCY = 440;
	// Half a second at the output rate
	const uint64_t TONE_FRAMES = MINIAUDIO_MANUAL_SAMPLE_RATE / 2;

	// A fresh engine for each test, so sounds from one test can't be heard in another
	std::unique_ptr<IAudioEngine> NewEngine()
	{
		std::unique_ptr<IAudioEngine> lpEngine(CreateMiniaudioEngine(MINIAUDIO_OUTPUT_MANUAL));
		REQUIRE(lpEngine.get());
		return lpEngine;
	}

	std::unique_ptr<IAudioSound> Open(IAudioEngine &engine, const char *lpszFixture, bool bLoop = false)
	{
		std::unique_ptr<IAudioSound> lpSound = engine.OpenSound(OpenFixture(lpszFixture), bLoop);
		REQUIRE(lpSound.get());
		return lpSound;
	}

	// Plays a fixture with some settings applied, and measures the output
	template <typename Setup> OutputMeasurement PlayAndMeasure(const char *lpszFixture, Setup setup)
	{
		std::unique_ptr<IAudioEngine> lpEngine = NewEngine();
		std::unique_ptr<IAudioSound> lpSound = Open(*lpEngine, lpszFixture);
		setup(*lpSound);
		REQUIRE(lpSound->Start());

		return MeasureOutput(RenderOutput(*lpEngine));
	}

	OutputMeasurement PlayAndMeasure(const char *lpszFixture)
	{
		return PlayAndMeasure(lpszFixture, [](IAudioSound &) {});
	}

	// Renders exactly nFrames frames
	std::vector<float> RenderFrames(IAudioEngine &engine, uint64_t nFrames)
	{
		std::vector<float> output(nFrames * MINIAUDIO_MANUAL_CHANNELS);
		REQUIRE(ReadMiniaudioEngineOutput(&engine, &output[0], nFrames));
		return output;
	}

	bool IsSilent(const std::vector<float> &output)
	{
		for (float nSample : output)
		{
			if (nSample != 0)
				return false;
		}
		return true;
	}
}

TEST_CASE("Only manual engines can be read from")
{
	std::unique_ptr<IAudioEngine> lpNullEngine(CreateMiniaudioEngine(MINIAUDIO_OUTPUT_NULL));
	REQUIRE(lpNullEngine.get());

	float frames[2 * 16];
	CHECK_FALSE(ReadMiniaudioEngineOutput(lpNullEngine.get(), frames, 16));
	CHECK_FALSE(ReadMiniaudioEngineOutput(NULL, frames, 16));
	CHECK(ReadMiniaudioEngineOutput(NewEngine().get(), frames, 16));
}

TEST_CASE("Every format decodes to the tone it holds, for its whole length")
{
	for (const char *lpszFixture : { "wav_s16_44100_stereo.wav", "wav_u8_22050_mono.wav", "wav_s24_48000_stereo.wav",
		"mp3_cbr_44100_stereo.mp3", "mp3_vbr_22050_mono.mp3", "mp3_id3v2_120k.mp3", "mp3_id3v1_trailer.mp3",
		"ogg_vorbis_44100_stereo.ogg", "ogg_vorbis_48000_mono.ogg", "flac_16_44100_stereo.flac", "flac_24_96000_stereo.flac" })
	{
		std::string name = lpszFixture;
		CAPTURE(name);

		OutputMeasurement measurement = PlayAndMeasure(lpszFixture);
		REQUIRE(measurement.bHeard);

		// Files at other rates than the output's are resampled, so the tone stays at 440 hz
		CHECK(measurement.left.nFrequency == Within(TONE_FREQUENCY, 0.03));
		CHECK(measurement.right.nFrequency == Within(TONE_FREQUENCY, 0.03));
		// Mono files play on both sides
		CHECK(measurement.left.nRms == Within(measurement.right.nRms, 0.02));
		CHECK(measurement.left.nRms > 0.05f);

		// Half a second, give or take what lossy codecs do at the edges
		CHECK(measurement.nAudibleFrames == Within((double)TONE_FRAMES, 0.03));
	}
}

TEST_CASE("Volume scales the output")
{
	OutputMeasurement reference = PlayAndMeasure("wav_s16_44100_stereo.wav");
	REQUIRE(reference.bHeard);

	for (float nVolume : { 0.5f, 0.1f, 0.8f })
	{
		CAPTURE(nVolume);
		OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", [nVolume](IAudioSound &sound) { sound.SetVolume(nVolume); });
		REQUIRE(measurement.bHeard);

		CHECK(measurement.left.nRms == Within(reference.left.nRms * nVolume, 0.02));
		CHECK(measurement.right.nRms == Within(reference.right.nRms * nVolume, 0.02));
		CHECK(measurement.left.nFrequency == Within(TONE_FREQUENCY, 0.03));
	}
}

TEST_CASE("Panning attenuates the other side, as DirectSound does")
{
	OutputMeasurement reference = PlayAndMeasure("wav_s16_44100_stereo.wav");
	REQUIRE(reference.bHeard);

	SUBCASE("Right")
	{
		OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", [](IAudioSound &sound) { sound.SetPan(0.5f); });
		CHECK(measurement.left.nRms == Within(reference.left.nRms * 0.5f, 0.02));
		CHECK(measurement.right.nRms == Within(reference.right.nRms, 0.02));
	}

	SUBCASE("Left")
	{
		OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", [](IAudioSound &sound) { sound.SetPan(-0.75f); });
		CHECK(measurement.left.nRms == Within(reference.left.nRms, 0.02));
		CHECK(measurement.right.nRms == Within(reference.right.nRms * 0.25f, 0.02));
	}

	SUBCASE("All the way")
	{
		OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", [](IAudioSound &sound) { sound.SetPan(1.0f); });
		CHECK(measurement.left.nRms < 1e-4f);
		CHECK(measurement.right.nRms == Within(reference.right.nRms, 0.02));
	}

	SUBCASE("Mono sounds pan too")
	{
		OutputMeasurement monoReference = PlayAndMeasure("wav_u8_22050_mono.wav");
		OutputMeasurement measurement = PlayAndMeasure("wav_u8_22050_mono.wav", [](IAudioSound &sound) { sound.SetPan(-0.5f); });
		CHECK(measurement.left.nRms == Within(monoReference.left.nRms, 0.02));
		CHECK(measurement.right.nRms == Within(monoReference.right.nRms * 0.5f, 0.02));
	}
}

TEST_CASE("Pitch changes the frequency and the duration")
{
	for (float nPitch : { 2.0f, 0.5f, 1.5f })
	{
		CAPTURE(nPitch);
		OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", [nPitch](IAudioSound &sound) { sound.SetPitch(nPitch); });
		REQUIRE(measurement.bHeard);

		CHECK(measurement.left.nFrequency == Within(TONE_FREQUENCY * nPitch, 0.03));
		CHECK(measurement.nAudibleFrames == Within(TONE_FRAMES / nPitch, 0.03));
	}

	SUBCASE("Also on top of resampling")
	{
		OutputMeasurement measurement = PlayAndMeasure("flac_24_96000_stereo.flac", [](IAudioSound &sound) { sound.SetPitch(2.0f); });
		CHECK(measurement.left.nFrequency == Within(TONE_FREQUENCY * 2, 0.03));
	}
}

TEST_CASE("Starting part of the way in plays the rest")
{
	OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", [](IAudioSound &sound) { REQUIRE(sound.SeekToFrame(TONE_FRAMES / 2)); });
	REQUIRE(measurement.bHeard);
	CHECK(measurement.nAudibleFrames == Within(TONE_FRAMES / 2.0, 0.03));
}

TEST_CASE("Muting silences a playing sound, and unmuting brings it back at its volume")
{
	std::unique_ptr<IAudioEngine> lpEngine = NewEngine();
	std::unique_ptr<IAudioSound> lpSound = Open(*lpEngine, "ogg_vorbis_44100_stereo.ogg", true);
	lpSound->SetVolume(0.5f);
	REQUIRE(lpSound->Start());

	// Wait until it's heard
	std::vector<float> heard = RenderOutput(*lpEngine, 2048, 0);
	REQUIRE_FALSE(IsSilent(heard));
	float nLevel = MeasureChannel(RenderFrames(*lpEngine, 4410), 0, 0, 4410).nRms;
	CHECK(nLevel > 0.01f);

	lpSound->SetMuted(true);
	CHECK(IsSilent(RenderFrames(*lpEngine, 4410)));

	// Changing the volume while muted doesn't unmute it
	lpSound->SetVolume(1.0f);
	CHECK(IsSilent(RenderFrames(*lpEngine, 4410)));

	lpSound->SetVolume(0.5f);
	lpSound->SetMuted(false);
	CHECK(MeasureChannel(RenderFrames(*lpEngine, 4410), 0, 0, 4410).nRms == Within(nLevel, 0.05));
}

TEST_CASE("Volume changes take effect while playing")
{
	std::unique_ptr<IAudioEngine> lpEngine = NewEngine();
	std::unique_ptr<IAudioSound> lpSound = Open(*lpEngine, "wav_s16_44100_stereo.wav", true);
	REQUIRE(lpSound->Start());

	REQUIRE_FALSE(IsSilent(RenderOutput(*lpEngine, 2048, 0)));
	float nFull = MeasureChannel(RenderFrames(*lpEngine, 4410), 0, 0, 4410).nRms;

	lpSound->SetVolume(0.25f);
	RenderFrames(*lpEngine, 256);
	CHECK(MeasureChannel(RenderFrames(*lpEngine, 4410), 0, 0, 4410).nRms == Within(nFull * 0.25f, 0.03));
}

TEST_CASE("Two sounds mix")
{
	std::unique_ptr<IAudioEngine> lpEngine = NewEngine();
	std::unique_ptr<IAudioSound> lpLeft = Open(*lpEngine, "wav_s16_44100_stereo.wav");
	std::unique_ptr<IAudioSound> lpRight = Open(*lpEngine, "flac_16_44100_stereo.flac");
	lpLeft->SetPan(-1.0f);
	lpRight->SetPan(1.0f);
	lpRight->SetPitch(2.0f);
	REQUIRE(lpLeft->Start());
	REQUIRE(lpRight->Start());

	OutputMeasurement measurement = MeasureOutput(RenderOutput(*lpEngine));
	REQUIRE(measurement.bHeard);
	CHECK(measurement.left.nFrequency == Within(TONE_FREQUENCY, 0.03));
	CHECK(measurement.right.nFrequency == Within(TONE_FREQUENCY * 2, 0.03));
}

namespace
{
	// Renders until something is heard, keeping only what was heard
	std::vector<float> RenderUntilHeard(IAudioEngine &engine)
	{
		std::chrono::steady_clock::time_point giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (std::chrono::steady_clock::now() < giveUp)
		{
			std::vector<float> block = RenderFrames(engine, 256);
			if (!IsSilent(block))
				return block;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		FAIL("nothing was heard");
		return std::vector<float>();
	}

	// The longest run of frames where both channels are near silent, between the first and last frames that aren't
	uint64_t LongestSilence(const std::vector<float> &output)
	{
		uint64_t nFrames = output.size() / MINIAUDIO_MANUAL_CHANNELS, nLongest = 0, nRun = 0;
		bool bHeard = false;
		for (uint64_t iFrame = 0; iFrame < nFrames; iFrame++)
		{
			bool bSilent = fabsf(output[iFrame * 2]) < 1e-3f && fabsf(output[iFrame * 2 + 1]) < 1e-3f;
			if (!bSilent)
			{
				if (bHeard && nRun > nLongest)
					nLongest = nRun;
				bHeard = true;
				nRun = 0;
			}
			else
				nRun++;
		}
		return nLongest;
	}
}

TEST_CASE("Fades change the level over time, on top of the volume")
{
	OutputMeasurement reference = PlayAndMeasure("wav_s16_44100_stereo.wav", [](IAudioSound &sound) { sound.SetVolume(0.5f); });

	std::unique_ptr<IAudioEngine> lpEngine = NewEngine();
	std::unique_ptr<IAudioSound> lpSound = Open(*lpEngine, "wav_s16_44100_stereo.wav");
	lpSound->SetVolume(0.5f);

	SUBCASE("A fade in starts silent and ends at the volume")
	{
		// Set before the sound starts, it begins when the sound does
		lpSound->Fade(0, 1, 400);
		REQUIRE(lpSound->Start());
		std::vector<float> output = RenderOutput(*lpEngine);

		ChannelMeasurement start = MeasureChannel(output, 0, 0, 1500);
		ChannelMeasurement middle = MeasureChannel(output, 0, 8000, 1500);
		ChannelMeasurement end = MeasureChannel(output, 0, TONE_FRAMES - 3500, 1500);
		CHECK(start.nRms < reference.left.nRms * 0.15f);
		CHECK(middle.nRms == Within(reference.left.nRms * 0.5f, 0.25));
		CHECK(end.nRms == Within(reference.left.nRms, 0.05));
	}

	SUBCASE("A fade out from where it is goes to silence, and the sound carries on silently")
	{
		REQUIRE(lpSound->Start());
		std::vector<float> output = RenderUntilHeard(*lpEngine);
		lpSound->Fade(-1, 0, 100);
		// Well short of the end of the half-second tone
		std::vector<float> rest = RenderFrames(*lpEngine, 10000);
		output.insert(output.end(), rest.begin(), rest.end());

		CHECK(MeasureChannel(output, 0, 0, 256).nRms == Within(reference.left.nRms, 0.1));
		// 100 ms is 4410 frames
		CHECK(MeasureChannel(output, 0, 6000, 4000).nRms < 1e-4f);
		CHECK(lpSound->IsPlaying());
	}

	SUBCASE("Muting still works during a fade")
	{
		lpSound->Fade(0, 1, 400);
		REQUIRE(lpSound->Start());
		RenderUntilHeard(*lpEngine);
		lpSound->SetMuted(true);
		CHECK(IsSilent(RenderFrames(*lpEngine, 4000)));
	}
}

TEST_CASE("A sound started after another plays the moment it ends, without a gap")
{
	std::unique_ptr<IAudioEngine> lpEngine = NewEngine();
	std::unique_ptr<IAudioSound> lpFirst = Open(*lpEngine, "wav_s16_44100_stereo.wav");
	std::unique_ptr<IAudioSound> lpSecond = Open(*lpEngine, "flac_16_44100_stereo.flac");

	REQUIRE(lpFirst->Start());
	// Once the first is actually playing, so that the time it ends at is known
	std::vector<float> output = RenderUntilHeard(*lpEngine);
	REQUIRE(lpSecond->StartAfter(*lpFirst));

	std::vector<float> rest = RenderOutput(*lpEngine, MINIAUDIO_MANUAL_SAMPLE_RATE * 3);
	output.insert(output.end(), rest.begin(), rest.end());

	OutputMeasurement measurement = MeasureOutput(output);
	// Both tones, one after the other, as one
	CHECK(measurement.nAudibleFrames == Within(2 * TONE_FRAMES, 0.02));
	CHECK(LongestSilence(output) < 32);
	CHECK(measurement.left.nFrequency == Within(TONE_FREQUENCY, 0.02));

	SUBCASE("Not after a looping sound, which doesn't end")
	{
		std::unique_ptr<IAudioSound> lpLooping = Open(*lpEngine, "wav_s16_44100_stereo.wav", true);
		std::unique_ptr<IAudioSound> lpAfter = Open(*lpEngine, "wav_s16_44100_stereo.wav");
		CHECK_FALSE(lpAfter->StartAfter(*lpLooping));
	}

	SUBCASE("Not after a sound of another engine")
	{
		std::unique_ptr<IAudioEngine> lpOtherEngine = NewEngine();
		std::unique_ptr<IAudioSound> lpOther = Open(*lpOtherEngine, "wav_s16_44100_stereo.wav");
		std::unique_ptr<IAudioSound> lpAfter = Open(*lpEngine, "wav_s16_44100_stereo.wav");
		CHECK_FALSE(lpAfter->StartAfter(*lpOther));
	}
}
