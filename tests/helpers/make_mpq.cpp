// Makes an MPQ for the tool's tests, with StormLib: make_mpq <archive> <name in the archive>=<file>...

#include <stdio.h>
#include <string>

#include <StormLib.h>

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: make_mpq <archive> <name in the archive>=<file>...\n");
		return 2;
	}

	remove(argv[1]);
	HANDLE hMpq = NULL;
	if (!SFileCreateArchive(argv[1], MPQ_CREATE_LISTFILE | MPQ_CREATE_ARCHIVE_V1, (DWORD)argc, &hMpq))
	{
		fprintf(stderr, "make_mpq: can't create %s\n", argv[1]);
		return 1;
	}

	int nResult = 0;
	for (int iArg = 2; iArg < argc; iArg++)
	{
		std::string arg = argv[iArg];
		size_t iEquals = arg.find('=');
		if (iEquals == std::string::npos || !SFileAddFileEx(hMpq, arg.substr(iEquals + 1).c_str(), arg.substr(0, iEquals).c_str(), MPQ_FILE_COMPRESS, MPQ_COMPRESSION_ZLIB, MPQ_COMPRESSION_NEXT_SAME))
		{
			fprintf(stderr, "make_mpq: can't add %s\n", argv[iArg]);
			nResult = 1;
		}
	}

	if (!SFileCloseArchive(hMpq))
		nResult = 1;

	return nResult;
}
