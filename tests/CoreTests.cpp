// Minimal dependency-free test runner for SpatialCore (no JUCE).
#include "tripan/Limits.h"

#include <cstdio>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (! (cond)) {                                                      \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

int main()
{
    CHECK (tripan::isValidChannelCount (1));
    CHECK (tripan::isValidChannelCount (64));
    CHECK (! tripan::isValidChannelCount (0));
    CHECK (! tripan::isValidChannelCount (65));

    if (failures == 0)
        std::printf ("All tests passed\n");
    return failures == 0 ? 0 : 1;
}
