// An IAudioEngine that plays nothing, but records what it was asked to do. Tests control whether sounds are still playing, and where their cursor is.

#ifndef FAKEAUDIOENGINE_H
#define FAKEAUDIOENGINE_H

#include "AudioEngine.h"

#include <string>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

struct FakeFade
{
	float nFrom;
	float nTo;
	uint32_t nMilliseconds;
};

// The state of one sound. Shared between the sound and the test, so the test can still look at it after the sound is destroyed.
struct FakeSoundState
{
	// Everything read from the file when the sound was opened
	std::vector<unsigned char> fileContents;
	bool bLoop = false;

	unsigned nChannels = 2;
	unsigned nSampleRate = 44100;

	bool bStarted = false;
	bool bDestroyed = false;
	uint64_t nSeekedTo = UINT64_MAX;
	// Set by StartAfter: the sound this one starts after
	std::shared_ptr<FakeSoundState> lpStartsAfter;
	std::vector<FakeFade> fades;

	float nVolume = 1;
	float nPan = 0;
	float nPitch = 1;
	bool bMuted = false;

	// Controlled by the test
	std::atomic<bool> bPlaying{true};
	uint64_t nCursor = 0;
	bool bLengthKnown = true;
	uint64_t nLength = 441000;
	// Whether StartAfter can work out when to start
	bool bCanStartAfter = true;

	// The fade level the last fade ends at, 1 if there is none
	float FadeTarget() const { return fades.empty() ? 1.0f : fades.back().nTo; }
	// The file contents as text, which is how tests tell their files apart
	std::string Contents() const { return std::string(fileContents.begin(), fileContents.end()); }
};

class FakeAudioSound : public IAudioSound
{
private:
	std::shared_ptr<FakeSoundState> m_state;

public:
	explicit FakeAudioSound(std::shared_ptr<FakeSoundState> state) : m_state(state) {}
	virtual ~FakeAudioSound() { m_state->bDestroyed = true; }

	std::shared_ptr<FakeSoundState> State() const { return m_state; }

	virtual unsigned GetChannels() const { return m_state->nChannels; }
	virtual unsigned GetSampleRate() const { return m_state->nSampleRate; }

	virtual bool SeekToFrame(uint64_t nFrame) { m_state->nSeekedTo = nFrame; m_state->nCursor = nFrame; return true; }
	virtual bool Start() { m_state->bStarted = true; return true; }
	virtual bool StartAfter(IAudioSound &previous)
	{
		FakeAudioSound *lpPrevious = dynamic_cast<FakeAudioSound *>(&previous);
		if (!m_state->bCanStartAfter || !lpPrevious)
			return false;

		m_state->bStarted = true;
		m_state->lpStartsAfter = lpPrevious->m_state;
		return true;
	}
	virtual void Fade(float nFrom, float nTo, uint32_t nMilliseconds) { m_state->fades.push_back(FakeFade{nFrom, nTo, nMilliseconds}); }

	virtual void SetVolume(float nVolume) { m_state->nVolume = nVolume; }
	virtual void SetPan(float nPan) { m_state->nPan = nPan; }
	virtual void SetPitch(float nRatio) { m_state->nPitch = nRatio; }
	virtual void SetMuted(bool bMuted) { m_state->bMuted = bMuted; }

	virtual bool IsPlaying() { return m_state->bPlaying; }
	virtual bool GetCursor(uint64_t *lpnFrame) { *lpnFrame = m_state->nCursor; return true; }
	virtual bool GetLength(uint64_t *lpnFrames)
	{
		if (!m_state->bLengthKnown)
			return false;

		*lpnFrames = m_state->nLength;
		return true;
	}
};

class FakeAudioEngine : public IAudioEngine
{
public:
	std::mutex lock;
	std::vector<std::shared_ptr<FakeSoundState>> sounds;

	// When false, OpenSound fails as if the file couldn't be decoded
	bool bCanDecode = true;
	// Channels and sampling rate of the sounds opened from now on
	unsigned nChannels = 2;
	unsigned nSampleRate = 44100;
	// Whether the sounds opened from now on can be scheduled with StartAfter
	bool bCanStartAfter = true;
	// Contents of files that can't be decoded, whatever bCanDecode says
	std::vector<std::string> undecodable;

	virtual std::unique_ptr<IAudioSound> OpenSound(std::unique_ptr<IAudioFile> lpFile, bool bLoop)
	{
		std::shared_ptr<FakeSoundState> state = std::make_shared<FakeSoundState>();
		state->bLoop = bLoop;
		state->nChannels = nChannels;
		state->nSampleRate = nSampleRate;
		state->bCanStartAfter = bCanStartAfter;

		// Read the whole file, the way a decoder would
		unsigned char buffer[1000];
		size_t nRead;
		while (lpFile->Read(buffer, sizeof(buffer), &nRead) && nRead > 0)
			state->fileContents.insert(state->fileContents.end(), buffer, buffer + nRead);

		{
			std::lock_guard<std::mutex> guard(lock);
			sounds.push_back(state);
		}

		if (!bCanDecode)
			return nullptr;
		for (const std::string &contents : undecodable)
		{
			if (state->Contents() == contents)
				return nullptr;
		}

		return std::unique_ptr<IAudioSound>(new FakeAudioSound(state));
	}

	std::shared_ptr<FakeSoundState> Last()
	{
		std::lock_guard<std::mutex> guard(lock);
		return sounds.empty() ? nullptr : sounds.back();
	}

	// The sound opened last from a file with these contents
	std::shared_ptr<FakeSoundState> Find(const std::string &contents)
	{
		std::lock_guard<std::mutex> guard(lock);
		for (size_t iSound = sounds.size(); iSound-- > 0;)
		{
			if (sounds[iSound]->Contents() == contents)
				return sounds[iSound];
		}
		return nullptr;
	}

	// How many sounds have been opened from files with these contents
	size_t CountOpened(const std::string &contents)
	{
		std::lock_guard<std::mutex> guard(lock);
		size_t nCount = 0;
		for (const std::shared_ptr<FakeSoundState> &state : sounds)
		{
			if (state->Contents() == contents)
				nCount++;
		}
		return nCount;
	}
};

#endif
