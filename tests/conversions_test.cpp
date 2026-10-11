// Tests for the conversions between DirectSound units and IAudioSound units. Platform-independent.

#include <doctest.h>

#include "DdaConversions.h"

TEST_CASE("DDA volume converts to linear amplitude")
{
	CHECK(DdaVolumeToLinear(DDA_VOLUME_MAX) == doctest::Approx(1.0f));
	CHECK(DdaVolumeToLinear(DDA_VOLUME_MIN) == 0.0f);

	// -20 dB is a tenth of the amplitude, -6 dB about half
	CHECK(DdaVolumeToLinear(-2000) == doctest::Approx(0.1f));
	CHECK(DdaVolumeToLinear(-600) == doctest::Approx(0.501f).epsilon(0.001));

	SUBCASE("Out of range values are clamped")
	{
		CHECK(DdaVolumeToLinear(DDA_VOLUME_MIN - 1) == 0.0f);
		CHECK(DdaVolumeToLinear(-100000) == 0.0f);
		CHECK(DdaVolumeToLinear(500) == doctest::Approx(1.0f));
	}

	SUBCASE("Louder settings are never quieter")
	{
		float nPrevious = DdaVolumeToLinear(DDA_VOLUME_MIN);
		for (long nVolume = DDA_VOLUME_MIN + 1; nVolume <= DDA_VOLUME_MAX; nVolume += 7)
		{
			float nLinear = DdaVolumeToLinear(nVolume);
			CHECK(nLinear >= nPrevious);
			nPrevious = nLinear;
		}
	}
}

TEST_CASE("DDA pan converts to linear balance")
{
	CHECK(DdaPanToLinear(0) == 0.0f);
	CHECK(DdaPanToLinear(DDA_PAN_LEFT) == -1.0f);
	CHECK(DdaPanToLinear(DDA_PAN_RIGHT) == 1.0f);

	SUBCASE("Negative is left, positive is right")
	{
		CHECK(DdaPanToLinear(-1) < 0.0f);
		CHECK(DdaPanToLinear(1) > 0.0f);
	}

	SUBCASE("The balance attenuates the other side as DirectSound does")
	{
		// Panning 20 dB to the right leaves the left side at a tenth of its amplitude, so the balance is 0.9
		CHECK(DdaPanToLinear(2000) == doctest::Approx(0.9f));
		CHECK(DdaPanToLinear(-2000) == doctest::Approx(-0.9f));
		// 6 dB is about half
		CHECK(DdaPanToLinear(600) == doctest::Approx(0.499f).epsilon(0.001));
	}

	SUBCASE("Symmetric")
	{
		for (long nPan = 0; nPan <= DDA_PAN_RIGHT; nPan += 13)
			CHECK(DdaPanToLinear(-nPan) == doctest::Approx(-DdaPanToLinear(nPan)));
	}

	SUBCASE("Out of range values are clamped")
	{
		CHECK(DdaPanToLinear(DDA_PAN_RIGHT + 1) == 1.0f);
		CHECK(DdaPanToLinear(-100000) == -1.0f);
	}
}

TEST_CASE("Pitch is relative to the game's buffer, not the sound")
{
	CHECK(DdaPitchRatio(22050, 22050) == doctest::Approx(1.0f));
	CHECK(DdaPitchRatio(44100, 22050) == doctest::Approx(2.0f));
	CHECK(DdaPitchRatio(11025, 22050) == doctest::Approx(0.5f));

	SUBCASE("Unknown frequencies leave the pitch alone")
	{
		CHECK(DdaPitchRatio(0, 22050) == 1.0f);
		CHECK(DdaPitchRatio(22050, 0) == 1.0f);
	}
}

TEST_CASE("Byte positions are bytes of 16-bit PCM")
{
	CHECK(DdaFramesToBytes(0, 2) == 0);
	CHECK(DdaFramesToBytes(1, 2) == 4);
	CHECK(DdaFramesToBytes(44100, 2) == 176400);
	CHECK(DdaFramesToBytes(44100, 1) == 88200);

	CHECK(DdaBytesToFrames(176400, 2) == 44100);
	CHECK(DdaBytesToFrames(88200, 1) == 44100);

	SUBCASE("Partial frames round down")
	{
		CHECK(DdaBytesToFrames(7, 2) == 1);
		CHECK(DdaBytesToFrames(3, 2) == 0);
	}

	SUBCASE("Round trip")
	{
		for (uint64_t nFrames = 0; nFrames < 100000; nFrames += 997)
		{
			CHECK(DdaBytesToFrames(DdaFramesToBytes(nFrames, 2), 2) == nFrames);
			CHECK(DdaBytesToFrames(DdaFramesToBytes(nFrames, 6), 6) == nFrames);
		}
	}

	SUBCASE("No channels gives no frames rather than dividing by zero")
	{
		CHECK(DdaBytesToFrames(1000, 0) == 0);
	}
}
