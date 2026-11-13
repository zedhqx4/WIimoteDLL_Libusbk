#pragma once
#include <cstdlib>
#include <iostream>

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::cerr << __FILE__ << ':' << __LINE__ \
                  << ": CHECK failed: " #expression << '\n'; \
        std::abort(); \
    } \
} while (false)
