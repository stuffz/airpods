#pragma once

// The stop flag both runners share, set from the signal handler.

#include <csignal>

namespace core
{

inline volatile std::sig_atomic_t stopRequested = 0;

inline void RequestStop(int)
{
    stopRequested = 1;
}

} // namespace core
