#include "KeyboardHookHandler.h"
#include "RTSSReader.h"
#include <Psapi.h>
#include "Keybind.h"
#include <optional>
#include "InputHandler.h"

KeyboardHookHandler::KeyboardHookHandler() {
	g_mainThreadId = GetCurrentThreadId();
}

bool KeyboardHookHandler::isMainThread() {
	return GetCurrentThreadId() == g_mainThreadId;
}

bool KeyboardHookHandler::addKeyboardHook() {
	if (isMainThread()) {
		keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, onKeyPress, GetModuleHandle(NULL), 0);
		return keyboardHook != NULL;
	} else {
		PostThreadMessage(g_mainThreadId, WM_USER_REHOOK_KEYBOARD, 0, 0);
		return true;
	}
}

void KeyboardHookHandler::removeKeyboardHook() {
	if (keyboardHook) {
		UnhookWindowsHookEx(keyboardHook);
		keyboardHook = NULL;
	}
}

bool KeyboardHookHandler::addMouseHook() {
	if (isMainThread()) {
		mouseHook = SetWindowsHookEx(WH_MOUSE_LL, onMouseEvent, GetModuleHandle(NULL), 0);
		return mouseHook != NULL;
	} else {
		PostThreadMessage(g_mainThreadId, WM_USER_REHOOK_MOUSE, 0, 0);
		return true;
	}
}

void KeyboardHookHandler::removeMouseHook() {
	if (mouseHook) {
		UnhookWindowsHookEx(mouseHook);
		mouseHook = NULL;
	}
}

std::string KeyboardHookHandler::getActiveProcessName() {
	HWND foregroundWindow = GetForegroundWindow();
	if (foregroundWindow == NULL) {
		return "No active window";
	}

	DWORD processId;
	GetWindowThreadProcessId(foregroundWindow, &processId);

	HANDLE processHandle = OpenProcess(
		PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
	if (processHandle == NULL) {
		return "Failed to open process";
	}

	TCHAR processName[MAX_PATH];
	if (GetModuleFileNameEx(processHandle, NULL, processName, MAX_PATH) == 0) {
		CloseHandle(processHandle);
		return "Failed to get process name";
	}

	CloseHandle(processHandle);

	// To get just the executable name from the full path
	std::wstring fullPath(processName, processName + lstrlen(processName));
	size_t lastBackslash = fullPath.find_last_of(L"\\");
	if (lastBackslash != std::wstring::npos) {
		return std::string(fullPath.begin() + lastBackslash + 1, fullPath.end());
	}

	return std::string(fullPath.begin(), fullPath.end());
}

bool KeyboardHookHandler::isChatRelatedKey(DWORD vkCode) {
	return vkCode == InputHandler::getInstance().findKey(CHAT_KEYBIND).value() ||
		vkCode == VK_RETURN || vkCode == VK_ESCAPE;
}

LRESULT CALLBACK KeyboardHookHandler::onKeyPress(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;

		if (pKeyBoard->flags & LLKHF_INJECTED ||
			getActiveProcessName() != RTSSReader::getInstance().targetProcess) {
			return CallNextHookEx(getInstance().keyboardHook, nCode, wParam, lParam);
		}
		switch (wParam) {

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN: {
			// lParam is a pointer to a KBDLLHOOKSTRUCT
			DWORD vkCode = pKeyBoard->vkCode;

			for (Keybind& keybind : Keybind::keybinds) {
				bool modifiersPressed = keybind.modifiers.size() != 0 ? std::all_of(keybind.modifiers.begin(), keybind.modifiers.end(), [](std::string modifier) {
					std::optional<WORD> key = InputHandler::getInstance().findKey(modifier);
					return InputHandler::getInstance().getPhysicalKeyState(key.value());
					}) : true;
				if (vkCode == keybind.keyCode && !keybind.isPressed && modifiersPressed) {
					keybind.isPressed = true;
					if (!getInstance().inChat || isChatRelatedKey(vkCode)) {
						keybind.function();
					}
					return 1;
				}
			}
			// printf("Key Down: %lu\n", vkCode);
			break;
		}

		case WM_KEYUP:
		case WM_SYSKEYUP: {
			DWORD vkCode = pKeyBoard->vkCode;
			for (Keybind& keybind : Keybind::keybinds) {
				if (vkCode == keybind.keyCode) {
					keybind.isPressed = false;
					return 1;
				}
			}
			// printf("Key Up: %lu\n", vkCode);
			break;
		}
		}
	}
	return CallNextHookEx(getInstance().keyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK KeyboardHookHandler::onMouseEvent(int nCode, WPARAM wParam, LPARAM lParam) {

	if (nCode == HC_ACTION) {
		MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;

		if ((pMouse->flags & LLMHF_INJECTED) ||
			getActiveProcessName() != RTSSReader::getInstance().targetProcess) {
			return CallNextHookEx(getInstance().mouseHook, nCode, wParam, lParam);
		}

		DWORD vkCode = 0;

		if (wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP) vkCode = VK_LBUTTON;
		else if (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP) vkCode = VK_RBUTTON;
		else if (wParam == WM_MBUTTONDOWN || wParam == WM_MBUTTONUP) vkCode = VK_MBUTTON;
		else if (wParam == WM_XBUTTONDOWN || wParam == WM_XBUTTONUP) {
			WORD xButton = HIWORD(pMouse->mouseData);
			if (xButton == XBUTTON1) vkCode = VK_XBUTTON1;
			else if (xButton == XBUTTON2) vkCode = VK_XBUTTON2;
		}

		if (vkCode == 0) return CallNextHookEx(NULL, nCode, wParam, lParam);

		switch (wParam) {

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_XBUTTONDOWN: {
			for (Keybind& keybind : Keybind::keybinds) {
				bool modifiersPressed = keybind.modifiers.size() != 0 ? std::all_of(keybind.modifiers.begin(), keybind.modifiers.end(), [](std::string modifier) {
					std::optional<WORD> key = InputHandler::getInstance().findKey(modifier);
					return InputHandler::getInstance().getPhysicalKeyState(key.value());
					}) : true;

				if (vkCode == keybind.keyCode && !keybind.isPressed && modifiersPressed) {
					keybind.isPressed = true;
					if (!getInstance().inChat) {
						keybind.function();
					}
					return 1;
				}
			}
			break;
		}

		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_XBUTTONUP: {
			for (Keybind& keybind : Keybind::keybinds) {
				if (vkCode == keybind.keyCode) {
					keybind.isPressed = false;
					return 1;
				}
			}
			break;
		}
		}
	}
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void KeyboardHookHandler::rehook() {
	removeKeyboardHook();
	removeMouseHook();
	addKeyboardHook();
	addMouseHook();
}