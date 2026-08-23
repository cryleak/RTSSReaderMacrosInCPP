#pragma once

#include <windows.h>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>
#include "RTSSSharedMemory.h"

enum class RtssState {
	WaitingForGta,
	RtssUnavailable,
	GameNotRegistered,
	Ready,
};

struct RtssStatus {
	RtssState state = RtssState::WaitingForGta;
	std::string targetProcess;
	DWORD targetPid = 0;
	uint64_t presentTime = 0;
	uint64_t generation = 0;
	std::string message;

	bool ready() const { return state == RtssState::Ready; }
};

class RTSSReader {
public:
	static RTSSReader& getInstance() {
		static RTSSReader instance;
		return instance;
	}

	RTSSReader(const RTSSReader&) = delete;
	void operator=(const RTSSReader&) = delete;

	RtssStatus refresh();
	const RtssStatus& status() const { return currentStatus; }
	uint64_t presentTime() const;
	void close();

private:
	RTSSReader() = default;
	~RTSSReader();

	struct Candidate {
		std::wstring name;
		DWORD pid = 0;
	};

	std::vector<Candidate> findCandidates() const;
	Candidate chooseCandidate(const std::vector<Candidate>& candidates) const;
	bool openMapping();
	void closeMapping();
	RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY* findProcess(const Candidate& candidate) const;
	uint64_t readMappedPresentTime() const;
	void setStatus(RtssState state, std::string message, const Candidate& candidate);

	HANDLE hMapFile = nullptr;
	RTSS_SHARED_MEMORY* pMapAddr = nullptr;
	RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY* pTargetApp = nullptr;
	mutable std::shared_mutex mappingMutex;
	RtssStatus currentStatus;
};
