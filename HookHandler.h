#pragma once

#include <windows.h>
#include <string>

#define WM_USER_REHOOK_KEYBOARD (WM_USER + 1)
#define WM_USER_REHOOK_MOUSE (WM_USER + 2)
#define CHAT_KEYBIND "t"

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

	static std::string getActiveProcessName();

	bool inChat = false;
private:
	HookHandler();
	~HookHandler() = default;
	HookHandler(const HookHandler&) = delete;
	void operator=(const HookHandler&) = delete;
	static LRESULT CALLBACK onKeyPress(int nCode, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK onMouseEvent(int nCode, WPARAM wParam, LPARAM lParam);
	bool isMainThread();

	HHOOK keyboardHook;
	HHOOK mouseHook;

	WORD g_mainThreadId = 0;

	static bool isChatRelatedKey(DWORD vkCode);
};