/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.

	The Initial Developer of the Original Code is Justin Olbrantz. The Original Code Copyright (C) 2009 Justin Olbrantz. All Rights Reserved.
*/

/*
	The interface between the DDA emulation and whatever actually decodes and plays the sounds. Keeping this free of Windows types lets the rest of the code be tested with a fake engine, and lets the engine itself be tested without Storm or the games.
*/

#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <memory>

enum AudioSeekOrigin
{
	AUDIO_SEEK_BEGIN,
	AUDIO_SEEK_CURRENT,
	AUDIO_SEEK_END
};

// Read access to the file a sound is streamed from. ThunderGraft implements this on top of the Storm SFile functions.
class IAudioFile
{
public:
	virtual ~IAudioFile() {}

	// Reads up to nBytes. Returns false on error; reading fewer bytes than requested means the end of the file was reached.
	virtual bool Read(void *lpBuffer, size_t nBytes, size_t *lpnBytesRead) = 0;
	virtual bool Seek(int64_t nOffset, AudioSeekOrigin origin) = 0;
	virtual bool Tell(int64_t *lpnPosition) = 0;
	virtual bool GetSize(uint64_t *lpnSize) = 0;
};

// A sound opened by an IAudioEngine. Destroying it stops it.
class IAudioSound
{
public:
	virtual ~IAudioSound() {}

	// The format the sound decodes to
	virtual unsigned GetChannels() const = 0;
	virtual unsigned GetSampleRate() const = 0;

	virtual bool SeekToFrame(uint64_t nFrame) = 0;
	virtual bool Start() = 0;
	// Starts the sound at the moment lpPrevious plays to its end, so the two play without a gap between them. Returns false, without starting it, if that moment can't be worked out, for example because lpPrevious's length isn't known or it loops; the caller then has to start it when lpPrevious ends.
	virtual bool StartAfter(IAudioSound &previous) = 0;

	// Fades from nFrom to nTo over nMilliseconds, starting now, or when the sound starts if it hasn't yet. A negative nFrom fades from wherever the last fade has got to. Levels are linear, 0 (silent) to 1 (unchanged); the fade level multiplies the volume and muting rather than replacing them.
	virtual void Fade(float nFrom, float nTo, uint32_t nMilliseconds) = 0;

	// Linear volume, 0 (silent) to 1 (unchanged)
	virtual void SetVolume(float nVolume) = 0;
	// Balance, -1 (left only) to 1 (right only). Panning to one side attenuates the other side, as in DirectSound.
	virtual void SetPan(float nPan) = 0;
	// Playback speed relative to the sound's own sampling rate; 1 is unchanged
	virtual void SetPitch(float nRatio) = 0;
	virtual void SetMuted(bool bMuted) = 0;

	// False once a sound that doesn't loop has played to its end
	virtual bool IsPlaying() = 0;
	virtual bool GetCursor(uint64_t *lpnFrame) = 0;
	// Returns false if the length is not known
	virtual bool GetLength(uint64_t *lpnFrames) = 0;
};

class IAudioEngine
{
public:
	virtual ~IAudioEngine() {}

	// Opens a sound for streaming from lpFile, which the sound takes ownership of. Returns NULL if the file can't be decoded. The sound starts out stopped.
	virtual std::unique_ptr<IAudioSound> OpenSound(std::unique_ptr<IAudioFile> lpFile, bool bLoop) = 0;
};

#endif // #ifndef AUDIOENGINE_H
