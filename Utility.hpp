
#pragma once

#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace Utility{

void FatalError(const char *s) noexcept
{
	std::perror(s);
	//std::exit(-1);
}


template <typename E>
constexpr auto to_underlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

/*
 * Utility function to convert a string to lower case.
 * */

void strtolower(char *str) {
    for (; *str; ++str)
        *str = (char)tolower(*str);
}


void *zh_malloc(size_t size) {
    void *buf = malloc(size);
    if (!buf) {
        fprintf(stderr, "Fatal error: unable to allocate memory.\n");
        exit(1);
    }
    return buf;
}

} // namespace Utility
