#pragma once

#include <windows.h>
#include <array>
#include <mutex>
#include <string>

constexpr UINT WM_APP_REHOOK_KEYBOARD = WM_APP + 1;
constexpr UINT WM_APP_REHOOK_MOUSE = WM_APP + 2;

class HookHandler {
public:
	static HookHandler& getInstance() {
		static HookHandler instance;
		return instance;
	}

	bool addKeyboardHook();
	void removeKeyboardHook();
	bool addMouseHook();
	void removeMouseHook();
	void rehook();

	void setTargetProcess(DWORD pid, std::string name);
	DWORD targetProcessId() const;
	std::string targetProcessName() const;
	bool isEnhancedTarget() const;
	bool isPhysicalKeyDown(WORD keyCode) const;
	void setMovementKeysBlocked(bool blocked);

	static std::string getActiveProcessName();

	bool inChat = false;

private:
	HookHandler();
	~HookHandler() = default;
	HookHandler(const HookHandler&) = delete;
	void operator=(const HookHandler&) = delete;
	static LRESULT CALLBACK onKeyPress(int nCode, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK onMouseEvent(int nCode, WPARAM wParam, LPARAM lParam);
	bool isMainThread() const;
	bool isTargetForeground() const;
	bool shouldBlockMovementKey(WORD keyCode) const;
	void setPhysicalKeyState(WORD keyCode, bool down);

	HHOOK keyboardHook = nullptr;
	HHOOK mouseHook = nullptr;
	DWORD mainThreadId = 0;
	mutable std::mutex targetMutex;
	DWORD targetPid = 0;
	std::string targetName;
	mutable std::mutex physicalStateMutex;
	std::array<bool, 256> physicalKeyStates{};
	bool movementKeysBlocked = false;
};
