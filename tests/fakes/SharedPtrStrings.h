// Lets doctest show shared_ptrs in failed checks, such as REQUIRE(file) or CHECK(a == b)

#ifndef SHAREDPTRSTRINGS_H
#define SHAREDPTRSTRINGS_H

#include <doctest.h>

#include <memory>
#include <stdio.h>

namespace doctest
{
	template <typename T> struct StringMaker<std::shared_ptr<T>>
	{
		static String convert(const std::shared_ptr<T> &pointer)
		{
			char szText[32];
			snprintf(szText, sizeof(szText), "%p", (const void *)pointer.get());
			return szText;
		}
	};
}

#endif
