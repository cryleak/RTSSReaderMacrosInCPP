#include "RTSSReader.h"
#include <iostream>
#include <TlHelp32.h>
#include <tchar.h>

RTSSReader::RTSSReader() {
    if (isProcessRunning(L"GTA5_Enhanced.exe")) {
        targetProcess = "GTA5_Enhanced.exe";
    }
    else if (isProcessRunning(L"GTA5.exe")) {
        targetProcess = "GTA5.exe";
    }
    else {
        std::cerr << "Could not find GTA process." << std::endl;
        Sleep(2000);
        exit(1);
    }

    hMapFile = OpenFileMappingW(FILE_MAP_READ, FALSE, L"RTSSSharedMemoryV2");
    if (!hMapFile) {
        std::cerr << "Could not open RTSS Shared Memory." << std::endl;
        Sleep(2000);
        exit(1);
    }

    pMapAddr = reinterpret_cast<RTSS_SHARED_MEMORY*>(MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0));
    if (!pMapAddr) {
        CloseHandle(hMapFile);
        std::cerr << "Failed to map view of shared memory." << std::endl;
        Sleep(2000);
        exit(1);
    }

    if (!findProcess(targetProcess)) {
        std::cerr << "Failed to find target process in RTSS.\n";
        Sleep(2000);
        exit(1);
    }
	std::cout << "RTSS reader initialized successfully for process: " << targetProcess << std::endl;
}

RTSSReader::~RTSSReader() {
    if (pMapAddr) UnmapViewOfFile(pMapAddr);
    if (hMapFile) CloseHandle(hMapFile);
}

bool RTSSReader::isProcessRunning(const TCHAR* processName) {
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (Process32First(snapshot, &entry)) {
        while (Process32Next(snapshot, &entry)) {
            if (_tcscmp(entry.szExeFile, processName) == 0) {
                CloseHandle(snapshot);
                return true;
            }
        }
    }
    CloseHandle(snapshot);
    return false;
}

bool RTSSReader::findProcess(const std::string& name) {
    if (!pMapAddr) return false;
    char* appArray = (char*)pMapAddr + pMapAddr->dwAppArrOffset;

    for (DWORD i = 0; i < pMapAddr->dwAppArrSize; ++i) {
        auto entry = (RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY*)(appArray + (i * pMapAddr->dwAppEntrySize));
        if (std::string(entry->szName).find(name) != std::string::npos) {
            pTargetApp = entry;
            return true;
        }
    }
    return false;
}

bool RTSSReader::isTargetAppStillRunning() {
    if (!pTargetApp) return true;

    DWORD pid = pTargetApp->dwProcessID;
    if (pid == 0) return false;

    static ULONGLONG lastKernelCheck = 0;
    ULONGLONG now = GetTickCount64();

    if (now - lastKernelCheck > 1000) {
        lastKernelCheck = now;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            DWORD exitCode;
            bool active = (GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE);
            CloseHandle(process);
            return active;
        }
        return false;
    }
    return true;
}