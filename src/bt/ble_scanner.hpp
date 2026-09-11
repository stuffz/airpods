#pragma once

#ifdef _WIN32
#include "bt/ble_scanner_windows.hpp" // IWYU pragma: export
#else
#include "bt/ble_scanner_linux.hpp" // IWYU pragma: export
#endif
