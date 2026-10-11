/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	Finds the length of an Ogg Vorbis file from its last page, without decoding it.

	The Vorbis decoder in miniaudio (stb_vorbis) can't tell the length of a file it reads through callbacks, which is how we read from Storm. Every Ogg page records the granule position at its end, which for Vorbis is the number of PCM frames decoded so far, so the granule position of the last page is the length of the stream.
*/

#ifndef OGGLENGTH_H
#define OGGLENGTH_H

#include <stddef.h>
#include <stdint.h>

#include "AudioEngine.h"

// The most that is read from the end of the file looking for the last page. The largest possible Ogg page is 65307 bytes, so this always holds at least one whole page.
#define OGG_LENGTH_SEARCH_BYTES (128 * 1024)

// Whether the file starts like an Ogg Vorbis stream. Leaves the file position undefined.
bool IsOggVorbis(IAudioFile &file);

// Gets the length in PCM frames of an Ogg Vorbis file. Returns false if the file isn't Ogg Vorbis, or its last page can't be found. Leaves the file position undefined.
bool GetOggVorbisLength(IAudioFile &file, uint64_t *lpnFrames);

// The CRC used by Ogg pages, over a page with its CRC field set to zero. Exposed for the tests.
uint32_t OggCrc(const unsigned char *lpData, size_t nBytes);
// Continues a CRC over more data; OggCrc(data) is OggCrcUpdate(0, data)
uint32_t OggCrcUpdate(uint32_t nCrc, const unsigned char *lpData, size_t nBytes);

#endif // #ifndef OGGLENGTH_H
