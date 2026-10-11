/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

#include "MiniaudioEngine.h"
#include "OggLength.h"

#include <stdio.h>
#include <string.h>
#include <map>
#include <new>

#include <miniaudio.h>

namespace
{
	/*
		miniaudio opens the files it streams by path, through a virtual file system (VFS). Ours hands it IAudioFiles: each one being opened is registered under a made-up path, "thundergraft:<id>", which the VFS looks up when miniaudio opens it.
	*/
	class CAudioFileVfs
	{
	private:
		// Must be the first member: miniaudio treats a pointer to the VFS as a pointer to its callbacks
		ma_vfs_callbacks m_callbacks;

		ma_mutex m_lock;
		std::map<unsigned, IAudioFile *> m_files;
		unsigned m_nNextID;

		static IAudioFile *File(ma_vfs_file file)
		{ return (IAudioFile *)file; }

		static ma_result OnOpen(ma_vfs *pVFS, const char *pFilePath, ma_uint32 openMode, ma_vfs_file *pFile)
		{
			if (!pFilePath || !pFile)
				return MA_INVALID_ARGS;
			if (openMode & MA_OPEN_MODE_WRITE)
				return MA_ACCESS_DENIED;

			unsigned nID;
			char cExtra;
			if (sscanf(pFilePath, "thundergraft:%u%c", &nID, &cExtra) != 1)
				return MA_DOES_NOT_EXIST;

			CAudioFileVfs *lpThis = (CAudioFileVfs *)pVFS;
			IAudioFile *lpFile = NULL;

			ma_mutex_lock(&lpThis->m_lock);
			std::map<unsigned, IAudioFile *>::iterator itFile = lpThis->m_files.find(nID);
			if (itFile != lpThis->m_files.end())
				lpFile = itFile->second;
			ma_mutex_unlock(&lpThis->m_lock);

			if (!lpFile)
				return MA_DOES_NOT_EXIST;

			*pFile = (ma_vfs_file)lpFile;

			return MA_SUCCESS;
		}

		static ma_result OnClose(ma_vfs *, ma_vfs_file)
		{
			// The file belongs to the sound, which closes it when it's destroyed
			return MA_SUCCESS;
		}

		static ma_result OnRead(ma_vfs *, ma_vfs_file file, void *pDst, size_t sizeInBytes, size_t *pBytesRead)
		{
			size_t nRead = 0;
			bool bSuccess = File(file)->Read(pDst, sizeInBytes, &nRead);
			if (pBytesRead)
				*pBytesRead = nRead;

			if (!bSuccess)
				return MA_IO_ERROR;
			if (!nRead && sizeInBytes)
				return MA_AT_END;

			return MA_SUCCESS;
		}

		static ma_result OnSeek(ma_vfs *, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
		{
			AudioSeekOrigin audioOrigin = AUDIO_SEEK_BEGIN;
			if (origin == ma_seek_origin_current)
				audioOrigin = AUDIO_SEEK_CURRENT;
			else if (origin == ma_seek_origin_end)
				audioOrigin = AUDIO_SEEK_END;

			return File(file)->Seek(offset, audioOrigin) ? MA_SUCCESS : MA_BAD_SEEK;
		}

		static ma_result OnTell(ma_vfs *, ma_vfs_file file, ma_int64 *pCursor)
		{
			int64_t nPosition;
			if (!File(file)->Tell(&nPosition))
				return MA_IO_ERROR;

			*pCursor = nPosition;

			return MA_SUCCESS;
		}

		static ma_result OnInfo(ma_vfs *, ma_vfs_file file, ma_file_info *pInfo)
		{
			uint64_t nSize;
			if (!File(file)->GetSize(&nSize))
				return MA_IO_ERROR;

			pInfo->sizeInBytes = nSize;

			return MA_SUCCESS;
		}

	public:
		CAudioFileVfs()
		{
			m_callbacks.onOpen = OnOpen;
			m_callbacks.onOpenW = NULL;
			m_callbacks.onClose = OnClose;
			m_callbacks.onRead = OnRead;
			m_callbacks.onWrite = NULL;
			m_callbacks.onSeek = OnSeek;
			m_callbacks.onTell = OnTell;
			m_callbacks.onInfo = OnInfo;

			m_nNextID = 1;

			ma_mutex_init(&m_lock);
		}

		~CAudioFileVfs()
		{
			ma_mutex_uninit(&m_lock);
		}

		ma_vfs *Vfs()
		{ return (ma_vfs *)this; }

		// Returns the ID to open the file by. Throws std::bad_alloc.
		unsigned Register(IAudioFile *lpFile)
		{
			ma_mutex_lock(&m_lock);

			unsigned nID = m_nNextID++;
			try
			{ m_files[nID] = lpFile; }
			catch (...)
			{
				ma_mutex_unlock(&m_lock);
				throw;
			}

			ma_mutex_unlock(&m_lock);

			return nID;
		}

		void Unregister(unsigned nID)
		{
			ma_mutex_lock(&m_lock);
			m_files.erase(nID);
			ma_mutex_unlock(&m_lock);
		}

		static void PathFor(unsigned nID, char (&szPath)[32])
		{
			snprintf(szPath, sizeof(szPath), "thundergraft:%u", nID);
		}
	};

	class CMiniaudioSound : public IAudioSound
	{
	private:
		std::unique_ptr<IAudioFile> m_lpFile;
		CAudioFileVfs &m_vfs;
		unsigned m_nFileID;

		ma_sound m_sound;
		bool m_bSoundInitialized;

		unsigned m_nChannels;
		unsigned m_nSampleRate;
		bool m_bLoop;
		bool m_bLengthKnown;
		uint64_t m_nLength;

		float m_nVolume;
		bool m_bMuted;

		void ApplyVolume()
		{ ma_sound_set_volume(&m_sound, m_bMuted ? 0.0f : m_nVolume); }

	public:
		CMiniaudioSound(std::unique_ptr<IAudioFile> lpFile, CAudioFileVfs &vfs, unsigned nFileID)
			: m_lpFile(std::move(lpFile)), m_vfs(vfs), m_nFileID(nFileID)
		{
			m_bSoundInitialized = false;
			m_nChannels = 0;
			m_nSampleRate = 0;
			m_bLoop = false;
			m_bLengthKnown = false;
			m_nLength = 0;
			m_nVolume = 1;
			m_bMuted = false;
		}

		virtual ~CMiniaudioSound()
		{
			// This stops the sound and waits for miniaudio to finish with the file, so the file can go after
			if (m_bSoundInitialized)
				ma_sound_uninit(&m_sound);

			m_vfs.Unregister(m_nFileID);
		}

		bool Open(ma_engine &engine, bool bLoop, bool bOggLengthKnown, uint64_t nOggLength)
		{
			char szPath[32];
			CAudioFileVfs::PathFor(m_nFileID, szPath);

			ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
			if (bLoop)
				flags |= MA_SOUND_FLAG_LOOPING;

			if (ma_sound_init_from_file(&engine, szPath, flags, NULL, NULL, &m_sound) != MA_SUCCESS)
				return false;

			m_bSoundInitialized = true;
			m_bLoop = bLoop;

			ma_uint32 nChannels = 0, nSampleRate = 0;
			if (ma_sound_get_data_format(&m_sound, NULL, &nChannels, &nSampleRate, NULL, 0) != MA_SUCCESS || !nChannels || !nSampleRate)
				return false;

			m_nChannels = nChannels;
			m_nSampleRate = nSampleRate;

			// The Vorbis decoder can't tell the length of streams, so it comes from the file's last page instead
			ma_uint64 nLength = 0;
			if (ma_sound_get_length_in_pcm_frames(&m_sound, &nLength) == MA_SUCCESS && nLength > 0)
			{
				m_bLengthKnown = true;
				m_nLength = nLength;
			}
			else if (bOggLengthKnown)
			{
				m_bLengthKnown = true;
				m_nLength = nOggLength;
			}

			// Panning to one side attenuates the other, as in DirectSound
			ma_sound_set_pan_mode(&m_sound, ma_pan_mode_balance);

			return true;
		}

		virtual unsigned GetChannels() const
		{ return m_nChannels; }

		virtual unsigned GetSampleRate() const
		{ return m_nSampleRate; }

		virtual bool SeekToFrame(uint64_t nFrame)
		{ return ma_sound_seek_to_pcm_frame(&m_sound, nFrame) == MA_SUCCESS; }

		virtual bool Start()
		{ return ma_sound_start(&m_sound) == MA_SUCCESS; }

		virtual void SetVolume(float nVolume)
		{
			m_nVolume = nVolume;
			ApplyVolume();
		}

		virtual void SetPan(float nPan)
		{ ma_sound_set_pan(&m_sound, nPan); }

		virtual void SetPitch(float nRatio)
		{
			if (nRatio > 0)
				ma_sound_set_pitch(&m_sound, nRatio);
		}

		virtual void SetMuted(bool bMuted)
		{
			m_bMuted = bMuted;
			ApplyVolume();
		}

		virtual bool IsPlaying()
		{ return !ma_sound_at_end(&m_sound); }

		virtual bool GetCursor(uint64_t *lpnFrame)
		{
			ma_uint64 nCursor;
			if (ma_sound_get_cursor_in_pcm_frames(&m_sound, &nCursor) != MA_SUCCESS)
				return false;

			// miniaudio only wraps the cursor of a looping stream if it knows the length itself
			if (m_bLoop && m_bLengthKnown && m_nLength > 0)
				nCursor %= m_nLength;

			*lpnFrame = nCursor;

			return true;
		}

		virtual bool GetLength(uint64_t *lpnFrames)
		{
			if (!m_bLengthKnown)
				return false;

			*lpnFrames = m_nLength;

			return true;
		}
	};

	class CMiniaudioEngine : public IAudioEngine
	{
	private:
		MiniaudioOutput m_output;

		CAudioFileVfs m_vfs;

		ma_resource_manager m_resourceManager;
		bool m_bResourceManagerInitialized;
		ma_context m_context;
		bool m_bContextInitialized;
		ma_engine m_engine;
		bool m_bEngineInitialized;

	public:
		CMiniaudioEngine()
		{
			m_output = MINIAUDIO_OUTPUT_DEFAULT;
			m_bResourceManagerInitialized = false;
			m_bContextInitialized = false;
			m_bEngineInitialized = false;
		}

		virtual ~CMiniaudioEngine()
		{
			if (m_bEngineInitialized)
				ma_engine_uninit(&m_engine);
			if (m_bResourceManagerInitialized)
				ma_resource_manager_uninit(&m_resourceManager);
			if (m_bContextInitialized)
				ma_context_uninit(&m_context);
		}

		bool Initialize(MiniaudioOutput output)
		{
			m_output = output;

			// The resource manager the engine would make for itself converts every sound to the output device's sampling rate as it decodes. Positions and lengths would then be in the device's frames rather than the file's, which the DDA functions report to the game. So keep each file's own rate and channels; the engine resamples as it mixes instead.
			ma_resource_manager_config resourceManagerConfig = ma_resource_manager_config_init();
			resourceManagerConfig.pVFS = m_vfs.Vfs();
			resourceManagerConfig.decodedFormat = ma_format_f32;
			if (ma_resource_manager_init(&resourceManagerConfig, &m_resourceManager) != MA_SUCCESS)
				return false;

			m_bResourceManagerInitialized = true;

			ma_engine_config engineConfig = ma_engine_config_init();
			engineConfig.pResourceManager = &m_resourceManager;

			if (output == MINIAUDIO_OUTPUT_NULL)
			{
				ma_backend backends[] = { ma_backend_null };
				if (ma_context_init(backends, 1, NULL, &m_context) != MA_SUCCESS)
					return false;

				m_bContextInitialized = true;
				engineConfig.pContext = &m_context;
			}
			else if (output == MINIAUDIO_OUTPUT_MANUAL)
			{
				engineConfig.noDevice = MA_TRUE;
				engineConfig.channels = MINIAUDIO_MANUAL_CHANNELS;
				engineConfig.sampleRate = MINIAUDIO_MANUAL_SAMPLE_RATE;
			}

			if (ma_engine_init(&engineConfig, &m_engine) != MA_SUCCESS)
				return false;

			m_bEngineInitialized = true;

			return true;
		}

		bool ReadOutput(float *lpFrames, uint64_t nFrames)
		{
			if (m_output != MINIAUDIO_OUTPUT_MANUAL)
				return false;

			ma_uint64 nRead = 0;
			ma_result result = ma_engine_read_pcm_frames(&m_engine, lpFrames, nFrames, &nRead);
			if (result != MA_SUCCESS && result != MA_AT_END)
				return false;

			// With nothing playing, the engine produces nothing rather than silence; but silence is what a device would play
			if (nRead < nFrames)
				memset(lpFrames + nRead * MINIAUDIO_MANUAL_CHANNELS, 0, (size_t)(nFrames - nRead) * MINIAUDIO_MANUAL_CHANNELS * sizeof(float));

			return true;
		}

		virtual std::unique_ptr<IAudioSound> OpenSound(std::unique_ptr<IAudioFile> lpFile, bool bLoop)
		{
			if (!lpFile)
				return nullptr;

			try
			{
				// Find the length of Ogg Vorbis files up front, before miniaudio starts reading the file on its own threads
				uint64_t nOggLength = 0;
				bool bOggLengthKnown = GetOggVorbisLength(*lpFile, &nOggLength);
				if (!lpFile->Seek(0, AUDIO_SEEK_BEGIN))
					return nullptr;

				IAudioFile *lpRawFile = lpFile.get();
				unsigned nFileID = m_vfs.Register(lpRawFile);

				std::unique_ptr<CMiniaudioSound> lpSound(new CMiniaudioSound(std::move(lpFile), m_vfs, nFileID));
				if (!lpSound->Open(m_engine, bLoop, bOggLengthKnown, nOggLength))
					return nullptr;

				return std::unique_ptr<IAudioSound>(lpSound.release());
			}
			catch (...)
			{ return nullptr; }
		}
	};
}

bool ReadMiniaudioEngineOutput(IAudioEngine *lpEngine, float *lpFrames, uint64_t nFrames)
{
	CMiniaudioEngine *lpMiniaudioEngine = dynamic_cast<CMiniaudioEngine *>(lpEngine);
	if (!lpMiniaudioEngine || !lpFrames)
		return false;

	return lpMiniaudioEngine->ReadOutput(lpFrames, nFrames);
}

IAudioEngine *CreateMiniaudioEngine(MiniaudioOutput output)
{
	CMiniaudioEngine *lpEngine = new (std::nothrow) CMiniaudioEngine();
	if (!lpEngine)
		return NULL;

	if (!lpEngine->Initialize(output))
	{
		delete lpEngine;
		return NULL;
	}

	return lpEngine;
}
