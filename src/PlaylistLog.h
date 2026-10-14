/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	ThunderGraft.log: problems with playlist files, in the same words as tgplaylist check, so that a user whose music doesn't change finds out why. It goes next to the game, or in the temporary directory if the game's directory can't be written to, and is only created once there is something to say, afresh each time the game starts.

	With THUNDERGRAFT_LOG=transitions in the environment, every transition is written too, which is for working out why a setup plays the wrong thing.

	No message boxes: the games run fullscreen, and a message box tends to minimise them or leave them stuck behind it.
*/

#ifndef PLAYLISTLOG_H
#define PLAYLISTLOG_H

#include <windows.h>
#include <string>

#include "PlaylistDiagnostics.h"
#include "PlaylistLayers.h"

#define PLAYLIST_LOG_NAME "ThunderGraft.log"

class CPlaylistLog : public IPlaylistLog
{
private:
	CRITICAL_SECTION m_csLock;
	std::string m_title;
	std::string m_directory;
	HANDLE m_hFile;
	bool m_bOpenFailed;
	bool m_bTrace;

	bool Open();
	void WriteLine(const std::string &text);

public:
	CPlaylistLog();
	// Leaves the file for Windows to close, as the destructor runs inside DllMain
	~CPlaylistLog() {}

	// lpszTitle heads the file. The log goes in lpszDirectory, which should be the game's directory, or in the temporary directory if that can't be written to.
	void Initialize(LPCSTR lpszTitle, LPCSTR lpszDirectory);

	// Where the log is, once it has been written to
	std::string Path();

	void Write(const PlaylistDiagnostics &diagnostics);
	void Write(const std::string &text);

	// IPlaylistLog
	virtual void Warning(const PlaylistDiagnostic &diagnostic);
	virtual bool TraceEnabled();
	virtual void Trace(const std::string &text);
};

#endif // #ifndef PLAYLISTLOG_H
