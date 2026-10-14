// Tests of playlists on the real engine, rendered without a device: what changing tracks sounds like. The fixtures are all half a second of a 440 hz tone. Platform-independent.

#include <doctest.h>

#include "fakes/SharedPtrStrings.h"

#include "MiniaudioEngine.h"
#include "PlaylistPlayer.h"

#include "fakes/OutputMeasurement.h"
#include "fakes/TestAudioFiles.h"

#include <math.h>

namespace
{
	const uint64_t TONE_FRAMES = MINIAUDIO_MANUAL_SAMPLE_RATE / 2;
	const PlaylistStreamId STREAM = 1;

	// Tracks are fixtures, named as they are in tests/fixtures
	class FixtureFiles : public IPlaylistFiles
	{
	public:
		virtual std::unique_ptr<IAudioFile> OpenTrack(const std::string &path)
		{ return OpenFixture(path); }

		virtual std::unique_ptr<IAudioFile> OpenGameFile(PlaylistStreamId)
		{ return OpenFixture("wav_u8_22050_mono.wav"); }
	};

	std::shared_ptr<const PlaylistFile> Parse(const std::string &text)
	{
		PlaylistReadOptions options;
		options.label = "playlist.toml";
		PlaylistDiagnostics diagnostics;
		std::shared_ptr<const PlaylistFile> file = ReadPlaylistFile(text, options, diagnostics);
		INFO(diagnostics.Format());
		REQUIRE(file);
		return file;
	}

	// Plays the music with the playlist file, calling Update between blocks as the plugin's update thread would, until the stream has ended and gone quiet. Returns what was heard, from the first sound on.
	std::vector<float> PlayAndRender(const std::string &playlist)
	{
		std::unique_ptr<IAudioEngine> lpEngine(CreateMiniaudioEngine(MINIAUDIO_OUTPUT_MANUAL));
		REQUIRE(lpEngine.get());
		FixtureFiles files;
		std::vector<float> output;

		{
			CPlaylistPlayer player;
			player.Initialize(lpEngine.get(), &files, NULL, 1);
			player.SetBase(Parse(playlist));

			std::string name = "music\\title.wav";
			REQUIRE(player.Begin(STREAM, &name, false, 0, PlaylistStreamSettings()));

			const uint64_t BLOCK_FRAMES = 256;
			std::vector<float> block(BLOCK_FRAMES * MINIAUDIO_MANUAL_CHANNELS);
			uint64_t nRendered = 0, nQuietFrames = 0;
			bool bHeard = false;
			while (nRendered < MINIAUDIO_MANUAL_SAMPLE_RATE * 5)
			{
				REQUIRE(ReadMiniaudioEngineOutput(lpEngine.get(), &block[0], BLOCK_FRAMES));
				nRendered += BLOCK_FRAMES;
				player.Update(nRendered * 1000 / MINIAUDIO_MANUAL_SAMPLE_RATE);

				bool bSilent = true;
				for (float nSample : block)
					bSilent = bSilent && nSample == 0;

				if (!bSilent)
					bHeard = true;
				if (bHeard)
					output.insert(output.end(), block.begin(), block.end());

				nQuietFrames = bSilent ? nQuietFrames + BLOCK_FRAMES : 0;
				if (bHeard && !player.IsPlaying(STREAM) && nQuietFrames > MINIAUDIO_MANUAL_SAMPLE_RATE / 4)
					break;
			}

			CHECK_FALSE(player.IsPlaying(STREAM));
			player.EndAll();
		}

		return output;
	}

	uint64_t LongestSilence(const std::vector<float> &output)
	{
		uint64_t nFrames = output.size() / MINIAUDIO_MANUAL_CHANNELS, nLongest = 0, nRun = 0;
		bool bHeard = false;
		for (uint64_t iFrame = 0; iFrame < nFrames; iFrame++)
		{
			if (fabsf(output[iFrame * 2]) >= 1e-3f || fabsf(output[iFrame * 2 + 1]) >= 1e-3f)
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

TEST_CASE("A playlist's tracks follow each other without a gap")
{
	std::vector<float> output = PlayAndRender("version = 1\n[on]\n'music\\*' = 'm'\n[playlist.m]\nplay = 'all'\ntracks = ['wav_s16_44100_stereo.wav', 'flac_16_44100_stereo.flac', 'ogg_vorbis_44100_stereo.ogg']\n");

	OutputMeasurement measurement = MeasureOutput(output);
	CHECK(measurement.nAudibleFrames == Within(3 * TONE_FRAMES, 0.02));
	CHECK(LongestSilence(output) < 32);
}

TEST_CASE("Tracks of different rates and channels follow each other without a gap too")
{
	std::vector<float> output = PlayAndRender("version = 1\n[on]\n'music\\*' = 'm'\n[playlist.m]\nplay = 'all'\ntracks = ['wav_u8_22050_mono.wav', 'flac_24_96000_stereo.flac']\n");

	OutputMeasurement measurement = MeasureOutput(output);
	CHECK(measurement.nAudibleFrames == Within(2 * TONE_FRAMES, 0.02));
	CHECK(LongestSilence(output) < 32);
}

TEST_CASE("A playlist with crossfade overlaps its tracks")
{
	// A crossfade of 0.2 seconds takes that much off the total
	std::vector<float> output = PlayAndRender("version = 1\n[on]\n'music\\*' = 'm'\n[playlist.m]\nplay = 'all'\ncrossfade = 0.2\ntracks = ['wav_s16_44100_stereo.wav', 'flac_16_44100_stereo.flac']\n");

	OutputMeasurement measurement = MeasureOutput(output);
	CHECK(measurement.nAudibleFrames == Within(2 * TONE_FRAMES - MINIAUDIO_MANUAL_SAMPLE_RATE / 5, 0.03));
	CHECK(LongestSilence(output) < 32);
}

TEST_CASE("next with fade_in starts the next playlist silent and brings it up")
{
	std::vector<float> output = PlayAndRender("version = 1\n[on]\n'music\\*' = 'a'\n[playlist.a]\ntracks = ['wav_s16_44100_stereo.wav']\nnext = { play = 'b', fade_in = 0.4 }\n[playlist.b]\ntracks = ['flac_16_44100_stereo.flac']\n");

	OutputMeasurement measurement = MeasureOutput(output);
	CHECK(measurement.nAudibleFrames == Within(2 * TONE_FRAMES, 0.03));

	// Just after the change, the second track is much quieter than the first was
	ChannelMeasurement before = MeasureChannel(output, 0, TONE_FRAMES - 3000, 2000);
	ChannelMeasurement after = MeasureChannel(output, 0, TONE_FRAMES + 200, 1500);
	ChannelMeasurement later = MeasureChannel(output, 0, 2 * TONE_FRAMES - 3000, 2000);
	CHECK(after.nRms < before.nRms * 0.2f);
	CHECK(later.nRms == Within(before.nRms, 0.1));
}
