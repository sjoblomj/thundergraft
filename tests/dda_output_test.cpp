// Tests of what comes out of the speakers when the DDA functions are used: CDdaPlayer on the real miniaudio engine, rendered without a device. The DDA settings are in DirectSound units, so these check the whole path from what the game asks for to what is heard.

#include <doctest.h>

#include "DdaPlayer.h"
#include "MiniaudioEngine.h"

#include "fakes/FakeDirectSoundBuffer.h"
#include "fakes/FakeStorm.h"
#include "fakes/OutputMeasurement.h"
#include "fakes/TestAudioFiles.h"

#include <math.h>

namespace
{
	const float TONE_FREQUENCY = 440;
	const uint64_t TONE_FRAMES = MINIAUDIO_MANUAL_SAMPLE_RATE / 2;

	// The amplitude factor of an attenuation in 1/100 dB, as DirectSound defines it
	float Attenuation(long nHundredthsOfDb)
	{
		return powf(10.0f, nHundredthsOfDb / 2000.0f);
	}

	struct OutputFixture
	{
		std::unique_ptr<IAudioEngine> lpEngine;
		CDdaPlayer player;
		std::vector<std::unique_ptr<FakeDirectSoundBuffer>> buffers;

		OutputFixture()
		{
			FakeStorm::Reset();
			lpEngine.reset(CreateMiniaudioEngine(MINIAUDIO_OUTPUT_MANUAL));
			REQUIRE(lpEngine.get());
			player.Initialize(FakeStorm::Functions(), lpEngine.get());
		}

		~OutputFixture()
		{
			// The sounds have to go before the engine
			player.StopAll();
		}

		HANDLE File(const char *lpszFixture)
		{
			std::vector<unsigned char> contents = ReadFixture(lpszFixture);
			REQUIRE_FALSE(contents.empty());
			return FakeStorm::AddFile(std::string(contents.begin(), contents.end()));
		}

		FakeDirectSoundBuffer &NewBuffer(DWORD nSampleRate = 22050)
		{
			buffers.emplace_back(new FakeDirectSoundBuffer(nSampleRate));
			return *buffers.back();
		}

		// Plays a fixture through the DDA functions to its end, and measures what was heard
		OutputMeasurement PlayAndMeasure(const char *lpszFixture, DWORD dwStartPos = 0, long nVolume = SFILE_DDA_DEFAULT, long nPan = SFILE_DDA_DEFAULT, IDirectSoundBuffer *lpBuffer = NULL)
		{
			HANDLE hFile = File(lpszFixture);
			REQUIRE(player.Begin(hFile, 0, 0x10000, dwStartPos, nVolume, nPan, lpBuffer));

			OutputMeasurement measurement = MeasureOutput(RenderOutput(*lpEngine));
			player.End(hFile);
			return measurement;
		}

		// The level of the left and right channels over the next stretch of output
		void MeasureLevels(float *lpnLeft, float *lpnRight)
		{
			const uint64_t FRAMES = 4410;
			std::vector<float> output(FRAMES * MINIAUDIO_MANUAL_CHANNELS);
			REQUIRE(ReadMiniaudioEngineOutput(lpEngine.get(), &output[0], FRAMES));
			*lpnLeft = MeasureChannel(output, 0, 0, FRAMES).nRms;
			*lpnRight = MeasureChannel(output, 1, 0, FRAMES).nRms;
		}

		// Starts a looping sound and waits until it's heard
		HANDLE StartLooping(const char *lpszFixture, IDirectSoundBuffer *lpBuffer = NULL)
		{
			HANDLE hFile = File(lpszFixture);
			REQUIRE(player.Begin(hFile, SFILE_DDA_LOOPING, 0x10000, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, lpBuffer));
			REQUIRE_FALSE(RenderOutput(*lpEngine, 4096, 0).empty());
			return hFile;
		}
	};
}

TEST_CASE_FIXTURE(OutputFixture, "DDA volumes are heard as DirectSound attenuations")
{
	OutputMeasurement reference = PlayAndMeasure("wav_s16_44100_stereo.wav");
	REQUIRE(reference.bHeard);

	for (long nVolume : { -600L, -2000L, -100L })
	{
		CAPTURE(nVolume);
		OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", 0, nVolume);
		CHECK(measurement.left.nRms == Within(reference.left.nRms * Attenuation(nVolume), 0.02));
		CHECK(measurement.right.nRms == Within(reference.right.nRms * Attenuation(nVolume), 0.02));
	}

	SUBCASE("The minimum is silence")
	{
		OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", 0, DSBVOLUME_MIN);
		CHECK_FALSE(measurement.bHeard);
	}
}

TEST_CASE_FIXTURE(OutputFixture, "DDA pan attenuates the other side by the same rule")
{
	OutputMeasurement reference = PlayAndMeasure("wav_s16_44100_stereo.wav");
	REQUIRE(reference.bHeard);

	SUBCASE("6 dB to the right")
	{
		OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", 0, SFILE_DDA_DEFAULT, 600);
		CHECK(measurement.left.nRms == Within(reference.left.nRms * Attenuation(-600), 0.02));
		CHECK(measurement.right.nRms == Within(reference.right.nRms, 0.02));
	}

	SUBCASE("20 dB to the left")
	{
		OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", 0, SFILE_DDA_DEFAULT, -2000);
		CHECK(measurement.left.nRms == Within(reference.left.nRms, 0.02));
		CHECK(measurement.right.nRms == Within(reference.right.nRms * Attenuation(-2000), 0.02));
	}

	SUBCASE("All the way")
	{
		OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", 0, SFILE_DDA_DEFAULT, DSBPAN_RIGHT);
		CHECK(measurement.left.nRms < 1e-4f);
		CHECK(measurement.right.nRms == Within(reference.right.nRms, 0.02));
	}
}

TEST_CASE_FIXTURE(OutputFixture, "The DDA start position, in bytes, skips that much of the sound")
{
	// Half of the 16-bit stereo tone
	OutputMeasurement measurement = PlayAndMeasure("mp3_cbr_44100_stereo.mp3", (DWORD)(TONE_FRAMES / 2 * 4));
	REQUIRE(measurement.bHeard);
	CHECK(measurement.nAudibleFrames == Within(TONE_FRAMES / 2.0, 0.03));
}

TEST_CASE_FIXTURE(OutputFixture, "SFileDdaSetVolume changes what is heard while playing")
{
	HANDLE hFile = StartLooping("ogg_vorbis_44100_stereo.ogg");
	float nLeft, nRight;
	MeasureLevels(&nLeft, &nRight);
	REQUIRE(nLeft > 0.01f);

	REQUIRE(player.SetVolume(hFile, -1200, 1200));

	float nNewLeft, nNewRight;
	MeasureLevels(&nNewLeft, &nNewRight);
	MeasureLevels(&nNewLeft, &nNewRight);
	CHECK(nNewLeft == Within(nLeft * Attenuation(-1200) * Attenuation(-1200), 0.05));
	CHECK(nNewRight == Within(nRight * Attenuation(-1200), 0.05));
}

TEST_CASE_FIXTURE(OutputFixture, "Non-global sounds go quiet while the game is in the background")
{
	StartLooping("wav_s16_44100_stereo.wav");
	float nLeft, nRight;
	MeasureLevels(&nLeft, &nRight);
	REQUIRE(nLeft > 0.01f);

	player.Update(FALSE);
	MeasureLevels(&nLeft, &nRight);
	MeasureLevels(&nLeft, &nRight);
	CHECK(nLeft == 0.0f);
	CHECK(nRight == 0.0f);

	player.Update(TRUE);
	MeasureLevels(&nLeft, &nRight);
	MeasureLevels(&nLeft, &nRight);
	CHECK(nLeft > 0.01f);
}

TEST_CASE_FIXTURE(OutputFixture, "Changes the game makes to its buffer are heard")
{
	FakeDirectSoundBuffer &buffer = NewBuffer(22050);
	StartLooping("wav_s16_44100_stereo.wav", &buffer);

	float nLeft, nRight;
	MeasureLevels(&nLeft, &nRight);
	REQUIRE(nLeft > 0.01f);

	SUBCASE("Volume")
	{
		buffer.SetVolume(-600);
		player.Update(TRUE);
		float nNewLeft, nNewRight;
		MeasureLevels(&nNewLeft, &nNewRight);
		MeasureLevels(&nNewLeft, &nNewRight);
		CHECK(nNewLeft == Within(nLeft * Attenuation(-600), 0.05));
	}

	SUBCASE("Frequency, relative to the buffer's (Diablo II)")
	{
		// The game doubles the frequency of a buffer it created for 22050 hz: the sound plays at twice its speed
		buffer.SetFrequency(44100);
		player.Update(TRUE);
		RenderOutput(*lpEngine, 512, 0);

		const uint64_t FRAMES = 8000;
		std::vector<float> output(FRAMES * MINIAUDIO_MANUAL_CHANNELS);
		REQUIRE(ReadMiniaudioEngineOutput(lpEngine.get(), &output[0], FRAMES));
		CHECK(MeasureChannel(output, 0, 0, FRAMES).nFrequency == Within(TONE_FREQUENCY * 2, 0.03));
	}
}

TEST_CASE_FIXTURE(OutputFixture, "A sound played through a game buffer at its own frequency plays at normal speed")
{
	// The buffer's frequency equals its rate, so the pitch is unchanged even though the file is 44100 hz and the buffer 22050
	FakeDirectSoundBuffer &buffer = NewBuffer(22050);
	OutputMeasurement measurement = PlayAndMeasure("wav_s16_44100_stereo.wav", 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, &buffer);
	REQUIRE(measurement.bHeard);
	CHECK(measurement.left.nFrequency == Within(TONE_FREQUENCY, 0.03));
	CHECK(measurement.nAudibleFrames == Within((double)TONE_FRAMES, 0.03));
}
