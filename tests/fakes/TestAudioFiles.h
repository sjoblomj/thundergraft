// IAudioFiles for tests: one reading a file from disk, one reading from memory

#ifndef TESTAUDIOFILES_H
#define TESTAUDIOFILES_H

#include "AudioEngine.h"

#include <stdio.h>
#include <string.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// The fixtures are copied next to the test programs, which run from there
inline std::string FixturePath(const std::string &name)
{
	return "fixtures/" + name;
}

inline std::vector<unsigned char> ReadFixture(const std::string &name)
{
	std::vector<unsigned char> contents;
	FILE *lpFile = fopen(FixturePath(name).c_str(), "rb");
	if (!lpFile)
		return contents;

	unsigned char buffer[4096];
	size_t nRead;
	while ((nRead = fread(buffer, 1, sizeof(buffer), lpFile)) > 0)
		contents.insert(contents.end(), buffer, buffer + nRead);

	fclose(lpFile);
	return contents;
}

// Reads from memory. Can count how often it's used, and report when it's destroyed.
class MemoryAudioFile : public IAudioFile
{
private:
	std::vector<unsigned char> m_contents;
	size_t m_nPosition = 0;
	std::shared_ptr<std::atomic<bool>> m_lpDestroyed;

public:
	explicit MemoryAudioFile(const std::vector<unsigned char> &contents, std::shared_ptr<std::atomic<bool>> lpDestroyed = nullptr)
		: m_contents(contents), m_lpDestroyed(lpDestroyed) {}

	virtual ~MemoryAudioFile()
	{
		if (m_lpDestroyed)
			*m_lpDestroyed = true;
	}

	virtual bool Read(void *lpBuffer, size_t nBytes, size_t *lpnBytesRead)
	{
		size_t nAvailable = m_contents.size() - m_nPosition;
		size_t nRead = nBytes < nAvailable ? nBytes : nAvailable;
		if (nRead)
			memcpy(lpBuffer, &m_contents[m_nPosition], nRead);
		m_nPosition += nRead;
		*lpnBytesRead = nRead;
		return true;
	}

	virtual bool Seek(int64_t nOffset, AudioSeekOrigin origin)
	{
		int64_t nBase = 0;
		if (origin == AUDIO_SEEK_CURRENT)
			nBase = (int64_t)m_nPosition;
		else if (origin == AUDIO_SEEK_END)
			nBase = (int64_t)m_contents.size();

		int64_t nNewPosition = nBase + nOffset;
		if (nNewPosition < 0 || nNewPosition > (int64_t)m_contents.size())
			return false;

		m_nPosition = (size_t)nNewPosition;
		return true;
	}

	virtual bool Tell(int64_t *lpnPosition)
	{
		*lpnPosition = (int64_t)m_nPosition;
		return true;
	}

	virtual bool GetSize(uint64_t *lpnSize)
	{
		*lpnSize = m_contents.size();
		return true;
	}
};

inline std::unique_ptr<IAudioFile> OpenFixture(const std::string &name, std::shared_ptr<std::atomic<bool>> lpDestroyed = nullptr)
{
	return std::unique_ptr<IAudioFile>(new MemoryAudioFile(ReadFixture(name), lpDestroyed));
}

#endif
