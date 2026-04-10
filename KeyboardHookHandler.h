#pragma once
#include <windows.h>
#include <string>

#define WM_USER_REHOOK_KEYBOARD (WM_USER + 1)
#define WM_USER_REHOOK_MOUSE (WM_USER + 2)
#define CHAT_KEYBIND "t"

class KeyboardHookHandler {
public:
	static KeyboardHookHandler& getInstance() {
		static KeyboardHookHandler instance;
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
	KeyboardHookHandler();
	~KeyboardHookHandler() = default;
	KeyboardHookHandler(const KeyboardHookHandler&) = delete;
	void operator=(const KeyboardHookHandler&) = delete;
	static LRESULT CALLBACK onKeyPress(int nCode, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK onMouseEvent(int nCode, WPARAM wParam, LPARAM lParam);
	bool isMainThread();

	HHOOK keyboardHook;
	HHOOK mouseHook;

	WORD g_mainThreadId = 0;

	static bool isChatRelatedKey(DWORD vkCode);
};