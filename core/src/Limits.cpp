#include "tripan/Limits.h"

namespace tripan
{
bool isValidChannelCount (int n) noexcept
{
    return n >= 1 && n <= kMaxChannels;
}
}
