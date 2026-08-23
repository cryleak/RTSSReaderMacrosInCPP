#pragma once
#include <string>
#include <windows.h>

bool SetProcessPriorityByName(const std::wstring& processName, DWORD priorityClass);
