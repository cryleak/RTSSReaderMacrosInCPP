#include "Utils.h"
#include <windows.h>
#include <string>
#include <TlHelp32.h>

bool SetProcessPriorityByName(const std::wstring& processName, DWORD priorityClass) {
    bool success = false;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            if (processName == pe.szExeFile) {
                HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe.th32ProcessID);

                if (hProcess != NULL) {
                    if (SetPriorityClass(hProcess, priorityClass)) {
                        success = true;
                    }
                    CloseHandle(hProcess);
                }
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return success;
}