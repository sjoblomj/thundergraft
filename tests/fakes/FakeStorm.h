// Fake Storm SFile functions working on in-memory files

#ifndef FAKESTORM_H
#define FAKESTORM_H

#include <windows.h>
#include <string>

#include "Storm.h"

namespace FakeStorm
{
	// Forgets all files
	void Reset();

	// Creates a file and returns its handle
	HANDLE AddFile(const std::string &contents);

	bool IsClosed(HANDLE hFile);
	int CloseCount(HANDLE hFile);

	STORMFILEFUNCTIONS Functions();
}

#endif
