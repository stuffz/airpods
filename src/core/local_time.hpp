#pragma once

#include <cstdlib>
#include <ctime>
#include <iostream>

namespace core
{

inline std::tm LocalTime(std::time_t value)
{
    std::tm result{};
#ifdef _WIN32
    if (localtime_s(&result, &value) != 0)
#else
    if (localtime_r(&value, &result) == nullptr)
#endif
    {
        std::cerr << "Cannot convert timestamp to local time" << std::endl;
        std::abort();
    }
    return result;
}

} // namespace core
