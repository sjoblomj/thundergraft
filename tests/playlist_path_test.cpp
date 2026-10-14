// Tests of file names and patterns as playlist files use them

#include <doctest.h>

#include "PlaylistPath.h"

TEST_CASE("Names are normalized to lower case and backslashes")
{
	CHECK(NormalizePlaylistName("MUSIC/Terran1.WAV") == "music\\terran1.wav");
	CHECK(NormalizePlaylistName("data\\global\\music\\Act1\\town1.wav") == "data\\global\\music\\act1\\town1.wav");
	CHECK(NormalizePlaylistName("") == "");

	SUBCASE("Only ASCII letters change case")
	{
		CHECK(NormalizePlaylistName("\xC5ngstr\xD6m.WAV") == "\xC5ngstr\xD6m.wav");
	}
}

TEST_CASE("Paths for Storm keep their case and use backslashes")
{
	CHECK(PlaylistPathForStorm("Music/Mine/T1.ogg") == "Music\\Mine\\T1.ogg");
	CHECK(PlaylistPathForStorm("music\\t1.ogg") == "music\\t1.ogg");
}

TEST_CASE("Patterns match whole names")
{
	CHECK(MatchPlaylistPattern("music\\title.wav", "music\\title.wav"));
	CHECK_FALSE(MatchPlaylistPattern("music\\title.wav", "music\\title.wav2"));
	CHECK_FALSE(MatchPlaylistPattern("music\\title.wav", "xmusic\\title.wav"));
	CHECK_FALSE(MatchPlaylistPattern("music\\title.wav", "music\\title.wa"));
	CHECK(MatchPlaylistPattern("", ""));
	CHECK_FALSE(MatchPlaylistPattern("", "x"));

	SUBCASE("? matches exactly one character")
	{
		CHECK(MatchPlaylistPattern("music\\terran?.wav", "music\\terran1.wav"));
		CHECK(MatchPlaylistPattern("music\\terran?.wav", "music\\terran3.wav"));
		CHECK_FALSE(MatchPlaylistPattern("music\\terran?.wav", "music\\terran.wav"));
		CHECK_FALSE(MatchPlaylistPattern("music\\terran?.wav", "music\\terran12.wav"));
	}

	SUBCASE("* matches any characters, including none and backslashes")
	{
		CHECK(MatchPlaylistPattern("music\\*", "music\\title.wav"));
		CHECK(MatchPlaylistPattern("music\\*", "music\\"));
		CHECK(MatchPlaylistPattern("music\\*", "music\\sub\\x.wav"));
		CHECK_FALSE(MatchPlaylistPattern("music\\*", "music"));
		CHECK(MatchPlaylistPattern("*", ""));
		CHECK(MatchPlaylistPattern("*.wav", "staredit\\wav\\boss.wav"));
		CHECK_FALSE(MatchPlaylistPattern("*.wav", "staredit\\wav\\boss.ogg"));
		CHECK(MatchPlaylistPattern("*\\boss*.wav", "staredit\\wav\\boss_intro.wav"));
		CHECK(MatchPlaylistPattern("a*b*c", "aXXbYYbZZc"));
		CHECK_FALSE(MatchPlaylistPattern("a*b*c", "aXXbYYbZZ"));
		CHECK(MatchPlaylistPattern("**", "anything"));
		CHECK(MatchPlaylistPattern("*?", "x"));
		CHECK_FALSE(MatchPlaylistPattern("*?", ""));
	}

	SUBCASE("Matching is on normalized names, so case matters to the function itself")
	{
		CHECK_FALSE(MatchPlaylistPattern("music\\title.wav", "MUSIC\\TITLE.WAV"));
		CHECK(MatchPlaylistPattern(NormalizePlaylistName("Music/Title.wav"), NormalizePlaylistName("MUSIC\\TITLE.WAV")));
	}
}

TEST_CASE("Specificity counts the characters that aren't wildcards")
{
	CHECK(PlaylistPatternSpecificity("music\\*") == 6);
	CHECK(PlaylistPatternSpecificity("music\\terran?.wav") == 16);
	CHECK(PlaylistPatternSpecificity("*") == 0);
	CHECK(PlaylistPatternSpecificity("") == 0);
}

TEST_CASE("A pattern without wildcards beats any with them, then the most specific wins")
{
	CHECK(ComparePlaylistPatterns("music\\terran1.wav", "music\\terran?.wav") > 0);
	CHECK(ComparePlaylistPatterns("music\\terran?.wav", "music\\*") > 0);
	CHECK(ComparePlaylistPatterns("music\\*", "music\\terran?.wav") < 0);
	CHECK(ComparePlaylistPatterns("music\\terran?.wav", "music\\zergxx?.wav*") == 0);
	CHECK(ComparePlaylistPatterns("music\\a.wav", "music\\b.wav") == 0);

	SUBCASE("Even when the wildcard pattern has as many other characters")
	{
		CHECK(ComparePlaylistPatterns("music\\x.wav", "music\\x.wav*") > 0);
		CHECK(ComparePlaylistPatterns("x", "*xyz*") > 0);
	}
}

TEST_CASE("Overlapping patterns are those some name matches both of")
{
	CHECK(PlaylistPatternsOverlap("music\\*", "*.wav"));
	CHECK(PlaylistPatternsOverlap("music\\terran?.wav", "music\\*1.wav"));
	CHECK(PlaylistPatternsOverlap("a", "a"));
	CHECK_FALSE(PlaylistPatternsOverlap("a", "b"));
	CHECK_FALSE(PlaylistPatternsOverlap("music\\*", "sound\\*"));
	CHECK_FALSE(PlaylistPatternsOverlap("*.wav", "*.ogg"));
	CHECK(PlaylistPatternsOverlap("?", "*"));
	CHECK_FALSE(PlaylistPatternsOverlap("?", ""));
	CHECK(PlaylistPatternsOverlap("", "*"));
	CHECK_FALSE(PlaylistPatternsOverlap("??", "?"));
	CHECK(PlaylistPatternsOverlap("a*b", "*c*"));
	CHECK_FALSE(PlaylistPatternsOverlap("a*b", "c*"));
	CHECK(PlaylistPatternsOverlap("*ab*", "*ba*"));
	CHECK(PlaylistPatternsOverlap("x?z", "?y?"));
	CHECK_FALSE(PlaylistPatternsOverlap("x?z", "?y"));

	SUBCASE("It agrees with matching on a few names")
	{
		const char *const patterns[] = { "music\\*", "*.wav", "music\\t?.wav", "*1*", "music\\t1.wav", "?????", "*" };
		const char *const names[] = { "music\\t1.wav", "music\\t2.ogg", "x1.wav", "abcde", "", "music\\" };
		for (const char *a : patterns)
		{
			for (const char *b : patterns)
			{
				bool bSomeNameMatchesBoth = false;
				for (const char *name : names)
				{
					if (MatchPlaylistPattern(a, name) && MatchPlaylistPattern(b, name))
						bSomeNameMatchesBoth = true;
				}

				// A name both match proves an overlap; the names aren't enough to disprove one
				if (bSomeNameMatchesBoth)
				{
					CAPTURE(a);
					CAPTURE(b);
					CHECK(PlaylistPatternsOverlap(a, b));
				}
			}
		}
	}
}

TEST_CASE("Control characters are found")
{
	CHECK(HasPlaylistControlCharacter("music\terran1.wav"));
	CHECK(HasPlaylistControlCharacter("a\nb"));
	CHECK(HasPlaylistControlCharacter("a\x7F"));
	CHECK(HasPlaylistControlCharacter(std::string("a\0b", 3)));
	CHECK_FALSE(HasPlaylistControlCharacter("music\\terran1.wav"));
	CHECK_FALSE(HasPlaylistControlCharacter("music/\xC3\xA5.ogg"));
}

TEST_CASE("Absolute paths are recognized")
{
	CHECK(IsAbsolutePlaylistPath("C:\\StarCraft\\music\\x.wav"));
	CHECK(IsAbsolutePlaylistPath("c:/x.ogg"));
	CHECK(IsAbsolutePlaylistPath("C:x.ogg"));
	CHECK(IsAbsolutePlaylistPath("\\x.ogg"));
	CHECK(IsAbsolutePlaylistPath("/x.ogg"));
	CHECK(IsAbsolutePlaylistPath("\\\\server\\share\\x.ogg"));
	CHECK_FALSE(IsAbsolutePlaylistPath("music\\x.ogg"));
	CHECK_FALSE(IsAbsolutePlaylistPath("x"));
	CHECK_FALSE(IsAbsolutePlaylistPath(""));
	CHECK_FALSE(IsAbsolutePlaylistPath("1:x"));
}

TEST_CASE("Parent references are found in any component")
{
	CHECK(HasPlaylistParentReference(".."));
	CHECK(HasPlaylistParentReference("..\\x.ogg"));
	CHECK(HasPlaylistParentReference("music\\..\\..\\x.ogg"));
	CHECK(HasPlaylistParentReference("music/../x.ogg"));
	CHECK(HasPlaylistParentReference("music\\.."));
	CHECK_FALSE(HasPlaylistParentReference("music\\..x.ogg"));
	CHECK_FALSE(HasPlaylistParentReference("music\\x...ogg"));
	CHECK_FALSE(HasPlaylistParentReference("music\\.\\x.ogg"));
	CHECK_FALSE(HasPlaylistParentReference(""));
}
