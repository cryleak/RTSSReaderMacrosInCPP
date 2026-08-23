#include "HookHandler.h"

#include "Gui.h"
#include "InputHandler.h"
#include "Keybind.h"

#include <Psapi.h>
#include <utility>

HookHandler::HookHandler() : mainThreadId(GetCurrentThreadId()) {}

bool HookHandler::isMainThread() const {
	return GetCurrentThreadId() == mainThreadId;
}

bool HookHandler::addKeyboardHook() {
	if (isMainThread()) {
		if (keyboardHook) return true;
		keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, onKeyPress, GetModuleHandleW(nullptr), 0);
		return keyboardHook != nullptr;
	}
	PostThreadMessageW(mainThreadId, WM_APP_REHOOK_KEYBOARD, 0, 0);
	return true;
}

void HookHandler::removeKeyboardHook() {
	if (keyboardHook) {
		UnhookWindowsHookEx(keyboardHook);
		keyboardHook = nullptr;
	}
}

bool HookHandler::addMouseHook() {
	if (isMainThread()) {
		if (mouseHook) return true;
		mouseHook = SetWindowsHookExW(WH_MOUSE_LL, onMouseEvent, GetModuleHandleW(nullptr), 0);
		return mouseHook != nullptr;
	}
	PostThreadMessageW(mainThreadId, WM_APP_REHOOK_MOUSE, 0, 0);
	return true;
}

void HookHandler::removeMouseHook() {
	if (mouseHook) {
		UnhookWindowsHookEx(mouseHook);
		mouseHook = nullptr;
	}
}

void HookHandler::setTargetProcess(DWORD pid, std::string name) {
	std::lock_guard lock(targetMutex);
	targetPid = pid;
	targetName = std::move(name);
}

DWORD HookHandler::targetProcessId() const {
	std::lock_guard lock(targetMutex);
	return targetPid;
}

std::string HookHandler::targetProcessName() const {
	std::lock_guard lock(targetMutex);
	return targetName;
}

bool HookHandler::isEnhancedTarget() const {
	std::lock_guard lock(targetMutex);
	return _stricmp(targetName.c_str(), "GTA5_Enhanced.exe") == 0;
}

bool HookHandler::isPhysicalKeyDown(WORD keyCode) const {
	std::lock_guard lock(physicalStateMutex);
	if (keyCode == VK_SHIFT) return physicalKeyStates[VK_LSHIFT] || physicalKeyStates[VK_RSHIFT];
	if (keyCode == VK_CONTROL) return physicalKeyStates[VK_LCONTROL] || physicalKeyStates[VK_RCONTROL];
	if (keyCode == VK_MENU) return physicalKeyStates[VK_LMENU] || physicalKeyStates[VK_RMENU];
	return keyCode < physicalKeyStates.size() && physicalKeyStates[keyCode];
}

void HookHandler::setMovementKeysBlocked(bool blocked) {
	std::lock_guard lock(physicalStateMutex);
	movementKeysBlocked = blocked;
}

bool HookHandler::shouldBlockMovementKey(WORD keyCode) const {
	std::lock_guard lock(physicalStateMutex);
	return movementKeysBlocked && (keyCode == 'A' || keyCode == 'D');
}

void HookHandler::setPhysicalKeyState(WORD keyCode, bool down) {
	if (keyCode >= physicalKeyStates.size()) return;
	std::lock_guard lock(physicalStateMutex);
	physicalKeyStates[keyCode] = down;
}

bool HookHandler::isTargetForeground() const {
	HWND foreground = GetForegroundWindow();
	if (!foreground) return false;
	DWORD foregroundPid = 0;
	GetWindowThreadProcessId(foreground, &foregroundPid);
	return foregroundPid != 0 && foregroundPid == targetProcessId();
}

std::string HookHandler::getActiveProcessName() {
	HWND foregroundWindow = GetForegroundWindow();
	if (!foregroundWindow) return "No active window";
	DWORD processId = 0;
	GetWindowThreadProcessId(foregroundWindow, &processId);
	HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, processId);
	if (!processHandle) return "Failed to open process";
	char processName[MAX_PATH]{};
	DWORD length = static_cast<DWORD>(std::size(processName));
	if (QueryFullProcessImageNameA(processHandle, 0, processName, &length) == 0) {
		CloseHandle(processHandle);
		return "Failed to get process name";
	}
	CloseHandle(processHandle);
	std::string fullPath(processName, length);
	size_t slash = fullPath.find_last_of("\\/");
	return slash == std::string::npos ? fullPath : fullPath.substr(slash + 1);
}

LRESULT CALLBACK HookHandler::onKeyPress(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		auto* keyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
		if (!(keyboard->flags & LLKHF_INJECTED)) {
			bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
			bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
			if (down || up) {
				getInstance().setPhysicalKeyState(static_cast<WORD>(keyboard->vkCode), down);
				if (NativeGui::captureKeyboardEvent(keyboard->vkCode, down)) return 1;
				if (getInstance().isTargetForeground()) {
					if (getInstance().shouldBlockMovementKey(static_cast<WORD>(keyboard->vkCode))) return 1;
					const bool legacyGame = !getInstance().isEnhancedTarget();
					if (down && legacyGame && getInstance().inChat && (keyboard->vkCode == VK_RETURN || keyboard->vkCode == VK_ESCAPE)) {
						getInstance().inChat = false;
						InputHandler::getInstance().queueInput(static_cast<WORD>(keyboard->vkCode), std::nullopt, false);
						return 1;
					}
					if (down && Keybind::dispatchKeyDown(static_cast<WORD>(keyboard->vkCode), legacyGame && getInstance().inChat, legacyGame)) return 1;
					if (up && Keybind::dispatchKeyUp(static_cast<WORD>(keyboard->vkCode))) return 1;
				}
			}
		}
	}
	return CallNextHookEx(getInstance().keyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK HookHandler::onMouseEvent(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		auto* mouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
		if (!(mouse->flags & LLMHF_INJECTED)) {
			WORD keyCode = 0;
			bool down = false;
			bool up = false;
			switch (wParam) {
			case WM_LBUTTONDOWN: keyCode = VK_LBUTTON; down = true; break;
			case WM_RBUTTONDOWN: keyCode = VK_RBUTTON; down = true; break;
			case WM_MBUTTONDOWN: keyCode = VK_MBUTTON; down = true; break;
			case WM_XBUTTONDOWN: keyCode = HIWORD(mouse->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2; down = true; break;
			case WM_LBUTTONUP: keyCode = VK_LBUTTON; up = true; break;
			case WM_RBUTTONUP: keyCode = VK_RBUTTON; up = true; break;
			case WM_MBUTTONUP: keyCode = VK_MBUTTON; up = true; break;
			case WM_XBUTTONUP: keyCode = HIWORD(mouse->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2; up = true; break;
			case WM_MOUSEWHEEL: keyCode = HIWORD(mouse->mouseData) & 0x8000 ? 0x1000 : 0x1001; down = true; break;
			case WM_MOUSEHWHEEL: keyCode = HIWORD(mouse->mouseData) & 0x8000 ? 0x1002 : 0x1003; down = true; break;
			}
			if (keyCode) {
				if (down || up) getInstance().setPhysicalKeyState(keyCode, down);
				if (NativeGui::captureMouseEvent(wParam, keyCode)) return 1;
				if (getInstance().isTargetForeground()) {
					const bool legacyGame = !getInstance().isEnhancedTarget();
					if (down && Keybind::dispatchMouseDown(keyCode, legacyGame && getInstance().inChat, legacyGame)) return 1;
					if (up && Keybind::dispatchMouseUp(keyCode)) return 1;
				}
			}
		}
	}
	return CallNextHookEx(getInstance().mouseHook, nCode, wParam, lParam);
}

void HookHandler::rehook() {
	removeKeyboardHook();
	removeMouseHook();
	addKeyboardHook();
	addMouseHook();
}
