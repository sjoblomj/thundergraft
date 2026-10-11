/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	An IAudioEngine built on miniaudio. It plays on an output device of its own rather than the game's DirectSound device, and streams sounds: reading and decoding happen on miniaudio's job thread, not in the audio callback, so slow reads (e.g. from CD) don't cause dropouts.

	Formats are detected from the file contents, never the name. Supported: WAV, MP3, Ogg Vorbis, FLAC.

	Platform-independent, so it can be tested anywhere.
*/

#ifndef MINIAUDIOENGINE_H
#define MINIAUDIOENGINE_H

#include "AudioEngine.h"

enum MiniaudioOutput
{
	// The system's default output device
	MINIAUDIO_OUTPUT_DEFAULT,
	// miniaudio's null device, which consumes audio in real time without playing it. For tests.
	MINIAUDIO_OUTPUT_NULL,
	// No device at all: the mix is only produced when read with ReadMiniaudioEngineOutput, as fast as it is read. For tests that check what would come out of the speakers.
	MINIAUDIO_OUTPUT_MANUAL
};

// The format a MINIAUDIO_OUTPUT_MANUAL engine mixes to: interleaved 32-bit float stereo at this rate
#define MINIAUDIO_MANUAL_SAMPLE_RATE 44100
#define MINIAUDIO_MANUAL_CHANNELS 2

// Creates an engine, or returns NULL if no output device could be opened. Must be destroyed on a thread where waiting for miniaudio's threads is safe (i.e. not in DllMain).
IAudioEngine *CreateMiniaudioEngine(MiniaudioOutput output);

// Mixes the next nFrames frames of a MINIAUDIO_OUTPUT_MANUAL engine's output into lpFrames. Returns false for any other engine. Sounds whose data hasn't been decoded yet contribute silence.
bool ReadMiniaudioEngineOutput(IAudioEngine *lpEngine, float *lpFrames, uint64_t nFrames);

#endif // #ifndef MINIAUDIOENGINE_H
