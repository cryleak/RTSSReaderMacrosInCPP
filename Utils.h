#pragma once
#include <chrono>
#include <string>
#include <windows.h>

bool SetProcessPriorityByName(const std::wstring& processName, DWORD priorityClass);

using TimingPoint = std::chrono::steady_clock::time_point;

inline TimingPoint startTiming()
{
    return std::chrono::steady_clock::now();
}

inline double stopTiming(TimingPoint start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
