#!/bin/sh
# Regenerates the audio files the tests play. They are committed, so running
# the tests doesn't need any of this; it's here to document how they were
# made, and for adding new ones. Needs ffmpeg (with libmp3lame and libvorbis)
# and python3.
#
# All the sounds are a 440 hz tone, half a second long unless noted, so their
# exact length in frames is known.

set -e
cd "$(dirname "$0")"

tone() {
	# tone <sample rate> <channels> <output> <ffmpeg encoding options...>
	rate=$1; channels=$2; output=$3; shift 3
	ffmpeg -nostdin -loglevel error -y -f lavfi \
		-i "sine=frequency=440:duration=0.5:sample_rate=$rate" \
		-ac "$channels" -map_metadata -1 -fflags +bitexact -flags:a +bitexact "$@" "$output"
}

tone 44100 2 wav_s16_44100_stereo.wav -c:a pcm_s16le
tone 22050 1 wav_u8_22050_mono.wav -c:a pcm_u8
tone 48000 2 wav_s24_48000_stereo.wav -c:a pcm_s24le

# No ID3v2 tag from ffmpeg, so the tagged variants below have exactly one
tone 44100 2 mp3_cbr_44100_stereo.mp3 -c:a libmp3lame -b:a 128k -id3v2_version 0
tone 22050 1 mp3_vbr_22050_mono.mp3 -c:a libmp3lame -q:a 4 -id3v2_version 0

tone 44100 2 ogg_vorbis_44100_stereo.ogg -c:a libvorbis -q:a 3
tone 48000 1 ogg_vorbis_48000_mono.ogg -c:a libvorbis -q:a 3

tone 44100 2 flac_16_44100_stereo.flac -c:a flac
tone 96000 2 flac_24_96000_stereo.flac -c:a flac -sample_fmt s32

python3 - <<'PYTHON'
import random

with open("mp3_cbr_44100_stereo.mp3", "rb") as f:
    mp3 = f.read()

# A big ID3v2 tag in front of the audio. FMOD Ex gave up looking for audio
# after 4 KB, which is why ThunderGraft once needed a workaround. Padding is
# legal in ID3v2, and counts towards the tag size.
tag_size = 120 * 1024
syncsafe = bytes([(tag_size >> 21) & 0x7F, (tag_size >> 14) & 0x7F, (tag_size >> 7) & 0x7F, tag_size & 0x7F])
with open("mp3_id3v2_120k.mp3", "wb") as f:
    f.write(b"ID3\x03\x00\x00" + syncsafe + bytes(tag_size) + mp3)

# An ID3v1 tag after the audio: 128 bytes starting with "TAG"
id3v1 = b"TAG" + b"ThunderGraft test".ljust(30, b"\0") + bytes(30) + bytes(30) + b"2026" + bytes(30) + b"\xff"
assert len(id3v1) == 128
with open("mp3_id3v1_trailer.mp3", "wb") as f:
    f.write(mp3 + id3v1)

# Things that aren't playable audio
with open("truncated.mp3", "wb") as f:
    f.write(mp3[:1500])
with open("not_audio.wav", "wb") as f:
    f.write(b"This is a text file that someone named .wav\n" * 20)
with open("empty.bin", "wb") as f:
    pass
random.seed(1234)
with open("garbage.bin", "wb") as f:
    f.write(bytes(random.getrandbits(8) for _ in range(4096)))
PYTHON

ls -l
