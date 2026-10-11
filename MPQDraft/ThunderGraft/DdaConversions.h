/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

/*
	Conversions between the units the DDA functions use (those of DirectSound) and the units of IAudioSound. Deliberately free of Windows headers so they can be unit tested anywhere.
*/

#ifndef DDACONVERSIONS_H
#define DDACONVERSIONS_H

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

// The same values as DSBVOLUME_MIN/MAX and DSBPAN_LEFT/RIGHT in dsound.h
#define DDA_VOLUME_MIN (-10000)
#define DDA_VOLUME_MAX 0
#define DDA_PAN_LEFT (-10000)
#define DDA_PAN_RIGHT 10000

// The DDA functions measure positions in bytes of 16-bit PCM, as if the sound were an uncompressed WAV
#define DDA_BYTES_PER_SAMPLE 2

// DirectSound takes exponential volume in 1/100 dB attenuation, IAudioSound uses linear from 0 to 1
inline float DdaVolumeToLinear(long nVolume)
{
	if (nVolume <= DDA_VOLUME_MIN)
		return 0;
	if (nVolume >= DDA_VOLUME_MAX)
		return 1;

	return powf(10.0f, nVolume / 2000.0f);
}

// DirectSound takes exponential -abs(nPanPos) in 1/100 dB attenuation for the lesser side, IAudioSound uses linear balance from -1 to 1
inline float DdaPanToLinear(long nPanPos)
{
	long nAtten = labs(nPanPos);
	if (nAtten >= -DDA_VOLUME_MIN)
		return (nPanPos < 0 ? -1.0f : 1.0f);

	// How much the lesser side is attenuated, as a linear factor from 0 (not at all) to 1 (silenced)
	float nLinearAtten = 1.0f - powf(10.0f, -nAtten / 2000.0f);

	return nLinearAtten * (nPanPos < 0 ? -1.0f : 1.0f);
}

// The playback speed for a sound played through an IDirectSoundBuffer whose frequency was set to nFrequency, when the buffer was created for nBufferFrequency. Diablo II seems to assume that all sounds play at 22050 hz, and sets frequencies on its buffers relative to that, so the frequency has to be taken relative to the buffer's rather than the sound's.
inline float DdaPitchRatio(unsigned long nFrequency, unsigned long nBufferFrequency)
{
	if (!nFrequency || !nBufferFrequency)
		return 1;

	return (float)nFrequency / (float)nBufferFrequency;
}

inline uint64_t DdaBytesToFrames(uint64_t nBytes, unsigned nChannels)
{
	if (!nChannels)
		return 0;

	return nBytes / (nChannels * DDA_BYTES_PER_SAMPLE);
}

inline uint64_t DdaFramesToBytes(uint64_t nFrames, unsigned nChannels)
{
	return nFrames * nChannels * DDA_BYTES_PER_SAMPLE;
}

#endif // #ifndef DDACONVERSIONS_H
