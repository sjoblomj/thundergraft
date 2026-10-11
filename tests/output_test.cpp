// Tests of what the miniaudio engine would actually send to the speakers, rendered without a device. The fixtures are all half a second of a 440 hz tone, so the level and frequency of each output channel show whether volume, pan, pitch and resampling were applied. Platform-independent.

#include <doctest.h>

#include "MiniaudioEngine.h"

#include "fakes/OutputMeasurement.h"
#include "fakes/TestAudioFiles.h"

#include <string>

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
