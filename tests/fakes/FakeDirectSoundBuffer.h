// An IDirectSoundBuffer that only keeps track of its settings, like the buffers games pass to SFileDdaBeginEx

#ifndef FAKEDIRECTSOUNDBUFFER_H
#define FAKEDIRECTSOUNDBUFFER_H

#include <windows.h>
#include <dsound.h>
#include <string.h>
#include <vector>

class FakeDirectSoundBuffer : public IDirectSoundBuffer
{
public:
	LONG nRefCount = 1;

	bool bPlaying = false;
	bool bLooping = false;
	int nPlayCount = 0;

	LONG nVolume = DSBVOLUME_MAX;
	LONG nPan = DSBPAN_CENTER;
	DWORD nFrequency;

	DWORD dwCapsFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;
	WAVEFORMATEX format;
	std::vector<BYTE> data;

	bool bLocked = false;

	FakeDirectSoundBuffer(DWORD nSampleRate = 22050, WORD nBitsPerSample = 16, WORD nChannels = 2, DWORD nBytes = 4096)
	{
		memset(&format, 0, sizeof(format));
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = nChannels;
		format.nSamplesPerSec = nSampleRate;
		format.wBitsPerSample = nBitsPerSample;
		format.nBlockAlign = nChannels * nBitsPerSample / 8;
		format.nAvgBytesPerSec = nSampleRate * format.nBlockAlign;

		nFrequency = nSampleRate;

		// Leftovers from some earlier sound, which must not be heard
		data.assign(nBytes, 0x37);
	}

	// IUnknown
	STDMETHOD(QueryInterface)(REFIID, void **ppvObject) { *ppvObject = NULL; return E_NOINTERFACE; }
	STDMETHOD_(ULONG, AddRef)() { return ++nRefCount; }
	STDMETHOD_(ULONG, Release)() { return --nRefCount; }

	// IDirectSoundBuffer
	STDMETHOD(GetCaps)(LPDSBCAPS lpCaps)
	{
		if (!lpCaps || lpCaps->dwSize != sizeof(DSBCAPS))
			return DSERR_INVALIDPARAM;

		lpCaps->dwFlags = dwCapsFlags;
		lpCaps->dwBufferBytes = (DWORD)data.size();
		lpCaps->dwUnlockTransferRate = 0;
		lpCaps->dwPlayCpuOverhead = 0;
		return DS_OK;
	}
	STDMETHOD(GetCurrentPosition)(LPDWORD lpdwPlay, LPDWORD lpdwWrite)
	{
		if (lpdwPlay) *lpdwPlay = 0;
		if (lpdwWrite) *lpdwWrite = 0;
		return DS_OK;
	}
	STDMETHOD(GetFormat)(LPWAVEFORMATEX lpFormat, DWORD dwSizeAllocated, LPDWORD lpdwSizeWritten)
	{
		DWORD nSize = sizeof(WAVEFORMATEX) < dwSizeAllocated ? sizeof(WAVEFORMATEX) : dwSizeAllocated;
		if (lpFormat)
			memcpy(lpFormat, &format, nSize);
		if (lpdwSizeWritten)
			*lpdwSizeWritten = nSize;
		return DS_OK;
	}
	STDMETHOD(GetVolume)(LPLONG lplVolume) { *lplVolume = nVolume; return DS_OK; }
	STDMETHOD(GetPan)(LPLONG lplPan) { *lplPan = nPan; return DS_OK; }
	STDMETHOD(GetFrequency)(LPDWORD lpdwFrequency) { *lpdwFrequency = nFrequency; return DS_OK; }
	STDMETHOD(GetStatus)(LPDWORD lpdwStatus)
	{
		*lpdwStatus = (bPlaying ? DSBSTATUS_PLAYING : 0) | (bPlaying && bLooping ? DSBSTATUS_LOOPING : 0);
		return DS_OK;
	}
	STDMETHOD(Initialize)(LPDIRECTSOUND, LPCDSBUFFERDESC) { return DSERR_ALREADYINITIALIZED; }
	STDMETHOD(Lock)(DWORD dwOffset, DWORD dwBytes, LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1, LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2, DWORD dwFlags)
	{
		if (bLocked)
			return DSERR_INVALIDCALL;

		if (dwFlags & DSBLOCK_ENTIREBUFFER)
		{
			dwOffset = 0;
			dwBytes = (DWORD)data.size();
		}
		if (dwOffset + dwBytes > data.size())
			return DSERR_INVALIDPARAM;

		bLocked = true;
		*ppvAudioPtr1 = &data[dwOffset];
		*pdwAudioBytes1 = dwBytes;
		if (ppvAudioPtr2) *ppvAudioPtr2 = NULL;
		if (pdwAudioBytes2) *pdwAudioBytes2 = 0;
		return DS_OK;
	}
	STDMETHOD(Play)(DWORD, DWORD, DWORD dwFlags)
	{
		bPlaying = true;
		bLooping = (dwFlags & DSBPLAY_LOOPING) != 0;
		nPlayCount++;
		return DS_OK;
	}
	STDMETHOD(SetCurrentPosition)(DWORD) { return DS_OK; }
	STDMETHOD(SetFormat)(LPCWAVEFORMATEX) { return DSERR_INVALIDCALL; }
	STDMETHOD(SetVolume)(LONG lVolume) { nVolume = lVolume; return DS_OK; }
	STDMETHOD(SetPan)(LONG lPan) { nPan = lPan; return DS_OK; }
	STDMETHOD(SetFrequency)(DWORD dwFrequency) { nFrequency = dwFrequency; return DS_OK; }
	STDMETHOD(Stop)() { bPlaying = false; return DS_OK; }
	STDMETHOD(Unlock)(LPVOID, DWORD, LPVOID, DWORD) { bLocked = false; return DS_OK; }
	STDMETHOD(Restore)() { return DS_OK; }

	bool IsSilent() const
	{
		BYTE silence = (format.wBitsPerSample == 8) ? 0x80 : 0;
		for (size_t i = 0; i < data.size(); i++)
		{
			if (data[i] != silence)
				return false;
		}
		return true;
	}
};

#endif
