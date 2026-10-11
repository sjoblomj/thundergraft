// Renders what a MINIAUDIO_OUTPUT_MANUAL engine would send to the speakers, and measures it. The test fixtures are all a 440 hz tone, so the level and frequency of each channel say whether volume, pan and pitch were applied.

#ifndef OUTPUTMEASUREMENT_H
#define OUTPUTMEASUREMENT_H

#include "MiniaudioEngine.h"

#include <doctest.h>

#include <math.h>
#include <stdint.h>
#include <chrono>
#include <thread>
#include <vector>

// A purely relative tolerance. doctest's Approx on its own allows epsilon * (1 + |value|), which for signal levels well below 1 accepts almost anything.
inline doctest::Approx Within(double nValue, double nRelativeTolerance)
{
	return doctest::Approx(nValue).epsilon(nRelativeTolerance).scale(0);
}

struct ChannelMeasurement
{
	// Root mean square level
	float nRms = 0;
	// Estimated from zero crossings; 0 for silence
	float nFrequency = 0;
};

struct OutputMeasurement
{
	// Whether anything was heard at all
	bool bHeard = false;
	// Frames from the first sound to the last
	uint64_t nAudibleFrames = 0;

	// Measured over a window starting a little after the sound starts
	ChannelMeasurement left;
	ChannelMeasurement right;
};

// Renders the engine's output until it has been silent for nTrailingSilence frames after something was heard, or nMaxFrames have been rendered. Leading silence, while the sound's first data is decoded on miniaudio's job thread, is skipped.
inline std::vector<float> RenderOutput(IAudioEngine &engine, uint64_t nMaxFrames = MINIAUDIO_MANUAL_SAMPLE_RATE * 3, uint64_t nTrailingSilence = MINIAUDIO_MANUAL_SAMPLE_RATE / 4)
{
	const uint64_t BLOCK_FRAMES = 256;
	const int MAX_WAIT_MS = 3000;

	std::vector<float> output;
	std::vector<float> block(BLOCK_FRAMES * MINIAUDIO_MANUAL_CHANNELS);
	bool bHeard = false;
	uint64_t nSilentFrames = 0;
	std::chrono::steady_clock::time_point giveUp = std::chrono::steady_clock::now() + std::chrono::milliseconds(MAX_WAIT_MS);

	while (output.size() / MINIAUDIO_MANUAL_CHANNELS < nMaxFrames)
	{
		if (!ReadMiniaudioEngineOutput(&engine, &block[0], BLOCK_FRAMES))
			break;

		bool bBlockSilent = true;
		for (float nSample : block)
		{
			if (nSample != 0)
			{
				bBlockSilent = false;
				break;
			}
		}

		if (!bHeard)
		{
			if (bBlockSilent)
			{
				// Nothing decoded yet; give the job thread a moment
				if (std::chrono::steady_clock::now() > giveUp)
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			bHeard = true;
		}

		output.insert(output.end(), block.begin(), block.end());

		nSilentFrames = bBlockSilent ? nSilentFrames + BLOCK_FRAMES : 0;
		if (nSilentFrames >= nTrailingSilence)
			break;
	}

	return output;
}

inline ChannelMeasurement MeasureChannel(const std::vector<float> &output, unsigned iChannel, uint64_t nStartFrame, uint64_t nFrames)
{
	ChannelMeasurement measurement;
	uint64_t nTotalFrames = output.size() / MINIAUDIO_MANUAL_CHANNELS;
	if (nStartFrame >= nTotalFrames)
		return measurement;
	if (nStartFrame + nFrames > nTotalFrames)
		nFrames = nTotalFrames - nStartFrame;

	double nSumOfSquares = 0;
	unsigned nCrossings = 0;
	float nPrevious = 0;
	for (uint64_t iFrame = nStartFrame; iFrame < nStartFrame + nFrames; iFrame++)
	{
		float nSample = output[iFrame * MINIAUDIO_MANUAL_CHANNELS + iChannel];
		nSumOfSquares += (double)nSample * nSample;

		// Count upward zero crossings: one per cycle
		if (iFrame > nStartFrame && nPrevious < 0 && nSample >= 0)
			nCrossings++;
		nPrevious = nSample;
	}

	measurement.nRms = (float)sqrt(nSumOfSquares / nFrames);
	if (measurement.nRms > 1e-4f)
		measurement.nFrequency = nCrossings * (float)MINIAUDIO_MANUAL_SAMPLE_RATE / nFrames;

	return measurement;
}

// Measures rendered output. The window skips the start of the sound, where resampling and decoders settle.
inline OutputMeasurement MeasureOutput(const std::vector<float> &output, uint64_t nSkipFrames = 1000, uint64_t nWindowFrames = 8000)
{
	OutputMeasurement measurement;
	uint64_t nTotalFrames = output.size() / MINIAUDIO_MANUAL_CHANNELS;

	// The first and last frames where anything is heard. Rendering skips leading silence, but a sound may start part of the way into the first block.
	uint64_t nFirst = nTotalFrames, nLast = 0;
	for (uint64_t iFrame = 0; iFrame < nTotalFrames; iFrame++)
	{
		for (unsigned iChannel = 0; iChannel < MINIAUDIO_MANUAL_CHANNELS; iChannel++)
		{
			if (fabsf(output[iFrame * MINIAUDIO_MANUAL_CHANNELS + iChannel]) > 1e-4f)
			{
				if (iFrame < nFirst)
					nFirst = iFrame;
				nLast = iFrame;
			}
		}
	}

	if (nFirst == nTotalFrames)
		return measurement;

	measurement.bHeard = true;
	measurement.nAudibleFrames = nLast - nFirst + 1;
	measurement.left = MeasureChannel(output, 0, nFirst + nSkipFrames, nWindowFrames);
	measurement.right = MeasureChannel(output, 1, nFirst + nSkipFrames, nWindowFrames);

	return measurement;
}

#endif
