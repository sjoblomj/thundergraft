// Fake Storm SFile functions working on in-memory files

#ifndef FAKESTORM_H
#define FAKESTORM_H

#include <windows.h>
#include <string>
#include <vector>

#include "Storm.h"

namespace FakeStorm
{
	// Forgets all files
	void Reset();

	// Creates a file and returns its handle
	HANDLE AddFile(const std::string &contents);

	// Makes a file that SFileOpenFileEx can open by name; each open gives a handle of its own, with its own position
	void AddNamedFile(const std::string &name, const std::string &contents);
	// The handles SFileOpenFileEx has given out, in order
	std::vector<HANDLE> OpenedHandles();

	bool IsClosed(HANDLE hFile);
	int CloseCount(HANDLE hFile);

	STORMFILEFUNCTIONS Functions();
}

#endif
