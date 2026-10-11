/*
	The implementation of miniaudio, with stb_vorbis for Ogg Vorbis. stb_vorbis's declarations have to come before miniaudio's implementation, and its implementation after, which is why this isn't simply miniaudio.c.

	The configuration (which backends and features are compiled in) is set by the build, as it has to be the same for everything that includes miniaudio.h.
*/

#define STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
