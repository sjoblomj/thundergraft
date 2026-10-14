// Files and a log for testing CPlaylistPlayer without Storm. Every file's contents say what it is, "track:<path>" or "game:<stream>", so tests can tell from FakeAudioEngine which file a sound plays.

#ifndef FAKEPLAYLISTFILES_H
#define FAKEPLAYLISTFILES_H

#include "PlaylistPlayer.h"

#include <string.h>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

class TextAudioFile : public IAudioFile
{
private:
	std::string m_contents;
	size_t m_nPosition = 0;

public:
	explicit TextAudioFile(const std::string &contents) : m_contents(contents) {}

	virtual bool Read(void *lpBuffer, size_t nBytes, size_t *lpnBytesRead)
	{
		size_t nRead = std::min(nBytes, m_contents.size() - m_nPosition);
		memcpy(lpBuffer, m_contents.data() + m_nPosition, nRead);
		m_nPosition += nRead;
		*lpnBytesRead = nRead;
		return true;
	}

	virtual bool Seek(int64_t nOffset, AudioSeekOrigin origin)
	{
		int64_t nBase = origin == AUDIO_SEEK_CURRENT ? (int64_t)m_nPosition : origin == AUDIO_SEEK_END ? (int64_t)m_contents.size() : 0;
		if (nBase + nOffset < 0 || nBase + nOffset > (int64_t)m_contents.size())
			return false;
		m_nPosition = (size_t)(nBase + nOffset);
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

class FakePlaylistFiles : public IPlaylistFiles
{
public:
	// Tracks that can't be found, as the playlist writes them
	std::set<std::string> missing;
	std::vector<std::string> openedTracks;
	std::vector<PlaylistStreamId> openedGameFiles;

	virtual std::unique_ptr<IAudioFile> OpenTrack(const std::string &path)
	{
		openedTracks.push_back(path);
		if (missing.count(path))
			return nullptr;
		return std::unique_ptr<IAudioFile>(new TextAudioFile("track:" + path));
	}

	virtual std::unique_ptr<IAudioFile> OpenGameFile(PlaylistStreamId id)
	{
		openedGameFiles.push_back(id);
		return std::unique_ptr<IAudioFile>(new TextAudioFile("game:" + std::to_string(id)));
	}
};

class FakePlaylistLog : public IPlaylistLog
{
public:
	std::vector<PlaylistDiagnostic> warnings;
	std::vector<std::string> trace;
	bool bTrace = true;

	virtual void Warning(const PlaylistDiagnostic &diagnostic)
	{ warnings.push_back(diagnostic); }

	virtual bool TraceEnabled()
	{ return bTrace; }

	virtual void Trace(const std::string &text)
	{ trace.push_back(text); }

	virtual std::string StreamName(PlaylistStreamId id)
	{ return "s" + std::to_string(id); }

	bool Traced(const std::string &text) const
	{
		for (const std::string &line : trace)
		{
			if (line.find(text) != std::string::npos)
				return true;
		}
		return false;
	}

	bool Warned(const std::string &text) const
	{
		for (const PlaylistDiagnostic &diagnostic : warnings)
		{
			if (diagnostic.message.find(text) != std::string::npos)
				return true;
		}
		return false;
	}

	std::string AllTrace() const
	{
		std::string text;
		for (const std::string &line : trace)
			text += line + "\n";
		return text;
	}
};

#endif
