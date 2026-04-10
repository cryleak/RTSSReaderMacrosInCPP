#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "RTSSSharedMemory.h"

class RTSSReader {
public:
    // Singleton Access
    static RTSSReader& getInstance() {
        static RTSSReader instance;
        return instance;
    }

    // Delete copy/assignment
    RTSSReader(const RTSSReader&) = delete;
    void operator=(const RTSSReader&) = delete;

    bool isTargetAppStillRunning();

    // Template must stay in the header
    template <typename T>
    T getAppMember(size_t offset) {
        if (!pTargetApp) return T();
        return *reinterpret_cast<T*>(reinterpret_cast<char*>(pTargetApp) + offset);
    }

    std::string targetProcess;

private:
    RTSSReader();  // Private constructor
    ~RTSSReader(); // Private destructor for cleanup

    bool findProcess(const std::string& name);
    bool isProcessRunning(const TCHAR* processName);

    // Members
    HANDLE hMapFile = nullptr;
    RTSS_SHARED_MEMORY* pMapAddr = nullptr;
    RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY* pTargetApp = nullptr;
};