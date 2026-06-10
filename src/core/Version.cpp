#include "Version.h"

// GIGGLE_VERSION is defined by CMake from the project() version.
#ifndef GIGGLE_VERSION
#define GIGGLE_VERSION "unknown"
#endif

namespace giggle {

const char* Version()
{
    return GIGGLE_VERSION;
}

} // namespace giggle
