#include "RTSSReader.h"

#include <TlHelp32.h>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <utility>

namespace
{
    // RTSS uses the MSVC four-character literal value for 'RTSS' (0x52545353).
    constexpr DWORD kRtssSignature = (static_cast<DWORD>('R') << 24) |
        (static_cast<DWORD>('T') << 16) |
        (static_cast<DWORD>('S') << 8) |
        static_cast<DWORD>('S');

    std::string narrow(const std::wstring& value)
    {
        if (value.empty()) return {};
        int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(count, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
        return result;
    }

    bool isCandidateName(const std::wstring& value)
    {
        return _wcsicmp(value.c_str(), L"GTA5.exe") == 0 || _wcsicmp(value.c_str(), L"GTA5_Enhanced.exe") == 0;
    }
}

RTSSReader::~RTSSReader()
{
    close();
}

std::vector<RTSSReader::Candidate> RTSSReader::findCandidates() const
{
    std::vector<Candidate> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (isCandidateName(entry.szExeFile)) result.push_back({entry.szExeFile, entry.th32ProcessID});
        }
        while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

RTSSReader::Candidate RTSSReader::chooseCandidate(const std::vector<Candidate>& candidates) const
{
    if (candidates.empty()) return {};
    HWND foreground = GetForegroundWindow();
    DWORD foregroundPid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
    for (const Candidate& candidate : candidates)
    {
        if (candidate.pid == foregroundPid) return candidate;
    }
    for (const Candidate& candidate : candidates)
    {
        if (candidate.pid == currentStatus.targetPid) return candidate;
    }
    for (const Candidate& candidate : candidates)
    {
        if (_wcsicmp(candidate.name.c_str(), L"GTA5_Enhanced.exe") == 0) return candidate;
    }
    return candidates.front();
}

bool RTSSReader::openMapping()
{
    if (pMapAddr) return true;
    hMapFile = OpenFileMappingW(FILE_MAP_READ, FALSE, L"RTSSSharedMemoryV2");
    if (!hMapFile) return false;
    pMapAddr = reinterpret_cast<RTSS_SHARED_MEMORY*>(MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0));
    if (!pMapAddr)
    {
        CloseHandle(hMapFile);
        hMapFile = nullptr;
        return false;
    }
    if (pMapAddr->dwSignature != kRtssSignature || (pMapAddr->dwVersion >> 16) != 2 ||
        pMapAddr->dwAppArrOffset == 0 || pMapAddr->dwAppEntrySize == 0 || pMapAddr->dwAppArrSize > 256)
    {
        closeMapping();
        return false;
    }
    return true;
}

void RTSSReader::closeMapping()
{
    pTargetApp = nullptr;
    if (pMapAddr) UnmapViewOfFile(pMapAddr);
    if (hMapFile) CloseHandle(hMapFile);
    pMapAddr = nullptr;
    hMapFile = nullptr;
}

RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY* RTSSReader::findProcess(const Candidate& candidate) const
{
    if (!pMapAddr) return nullptr;
    char* appArray = reinterpret_cast<char*>(pMapAddr) + pMapAddr->dwAppArrOffset;
    for (DWORD i = 0; i < pMapAddr->dwAppArrSize; ++i)
    {
        auto* entry = reinterpret_cast<RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY*>(appArray + (i * pMapAddr->dwAppEntrySize));
        if (entry->dwProcessID != candidate.pid) continue;
        const std::string candidateName = narrow(candidate.name);
        if (_stricmp(entry->szName, candidateName.c_str()) == 0 ||
            std::string(entry->szName).find(candidateName) != std::string::npos)
            return entry;
    }
    return nullptr;
}

void RTSSReader::setStatus(RtssState state, std::string message, const Candidate& candidate)
{
    if (currentStatus.state != state || currentStatus.targetPid != candidate.pid ||
        currentStatus.targetProcess != narrow(candidate.name))
    {
        ++currentStatus.generation;
    }
    currentStatus.state = state;
    currentStatus.targetProcess = candidate.name.empty() ? std::string() : narrow(candidate.name);
    currentStatus.targetPid = candidate.pid;
    currentStatus.message = std::move(message);
    if (state != RtssState::Ready)
    {
        currentStatus.presentTime = 0;
    }
}

RtssStatus RTSSReader::refresh()
{
    const std::vector<Candidate> candidates = findCandidates();
    const Candidate candidate = chooseCandidate(candidates);
    std::unique_lock lock(mappingMutex);
    if (pMapAddr && (pMapAddr->dwSignature != kRtssSignature || (pMapAddr->dwVersion >> 16) != 2)) closeMapping();
    if (candidate.pid == 0)
    {
        closeMapping();
        setStatus(RtssState::WaitingForGta, "Waiting for GTA5.exe or GTA5_Enhanced.exe", Candidate{});
        return currentStatus;
    }
    if (!openMapping())
    {
        setStatus(RtssState::RtssUnavailable, "RTSS shared memory is unavailable", candidate);
        return currentStatus;
    }
    pTargetApp = findProcess(candidate);
    if (!pTargetApp)
    {
        closeMapping();
        if (openMapping()) pTargetApp = findProcess(candidate);
    }
    if (!pTargetApp)
    {
        const bool mappingAvailable = pMapAddr != nullptr;
        setStatus(mappingAvailable ? RtssState::GameNotRegistered : RtssState::RtssUnavailable,
                  mappingAvailable ? "GTA is running but is not registered in RTSS" : "RTSS shared memory is unavailable", candidate);
        return currentStatus;
    }
    setStatus(RtssState::Ready, "RTSS data ready", candidate);
    currentStatus.presentTime = readMappedPresentTime();
    return currentStatus;
}

uint64_t RTSSReader::presentTime() const
{
    std::shared_lock lock(mappingMutex);
    return readMappedPresentTime();
}

uint64_t RTSSReader::readMappedPresentTime() const
{
    if (!pTargetApp) return 0;
    return *reinterpret_cast<const uint64_t*>(reinterpret_cast<const char*>(pTargetApp) +
        offsetof(RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY, qwPresentEndTime));
}

void RTSSReader::close()
{
    std::unique_lock lock(mappingMutex);
    closeMapping();
    currentStatus = {};
}
