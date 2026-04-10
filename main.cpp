#include "keymap.h"
#include <Psapi.h>
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mmsystem.h>
#include <mutex>
#include <optional>
#include <profileapi.h>
#include <queue>
#include <regex>
#include <stdio.h>
#include <string>
#include <tchar.h>
#include <thread>
#include <tlhelp32.h>
#include <vector>
#include <winnt.h>
#include <winuser.h>
#include <iostream>
#include "RTSSReader.h"
#include "Keybind.h"
#include "InputHandler.h"

#pragma comment(lib, "winmm.lib")
using namespace std::chrono_literals;

#define WM_USER_REHOOK (WM_USER + 1)

HHOOK keyboardHook;
HHOOK mouseHook;
WORD g_mainThreadId = 0;

static bool isMainThread() {
	return GetCurrentThreadId() == g_mainThreadId;
}

LRESULT CALLBACK onKeyPress(int nCode, WPARAM wParam, LPARAM lParam);

static bool addKeyboardHook() {
	if (isMainThread()) {
		keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, onKeyPress, GetModuleHandle(NULL), 0);
		if (keyboardHook == NULL) {
			return false;
		}
		return true;
	}
	else {
		PostThreadMessage(g_mainThreadId, WM_USER_REHOOK, 0, 0);
	}
}

static void removeKeyboardHook() {
	if (keyboardHook) {
		UnhookWindowsHookEx(keyboardHook);
		keyboardHook = NULL;
	}
}

std::string getActiveProcessName() {
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

bool inChat = false;

// macros
#define CONCAT(a, b) a##b
#define EXPAND_AND_CONCAT(a, b) CONCAT(a, b)
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define KEY_DOWN(k) k " down"
#define KEY_DOWN_R(k) k " downR"
#define KEY_UP(k) k " up"
#define KEY_UP_R(k) k " upR"

#define USE_CURSOR_MACROS 1
#define REPRESS_LEFT_CLICK 1

// ingame keybinds
#define INT_MENU_KEYBIND m
#define INT_MENU_KEY TOSTRING(INT_MENU_KEYBIND)
#define INT_MENU_KEY_R TOSTRING(EXPAND_AND_CONCAT(INT_MENU_KEYBIND, R))

#define RPG_KEY "2"
#define SNIPER_KEY "1"
#define STICKY_BOMB_KEY "4"
#define WEAPON_KEY_1 "3"
#define WEAPON_KEY_2 "5"
#define WEAPON_KEY_3 "6"
#define WEAPON_KEY_4 "7"
#define WEAPON_KEY_5 "8"
#define CHAT_KEYBIND "t"

// macro keybinds
#define BST_KEY 220
#define THERMAL_KEY 221
#define SNACKS_KEY 186
#define AMMO_KEY "F2"
#define RPG_SPAM_KEY "f24" // i don't use this
#define SNIPER_SPAM_KEY "q"

bool prepareForIntMenuAndCacheLeftClickState() {
	InputHandler::getInstance().queueInputs({ "lbutton upR", "rbutton upR" });
	return InputHandler::getInstance().getPhysicalKeyState(InputHandler::getInstance().findKey("lbutton").value());
}

void ensureIntMenuClose() { InputHandler::getInstance().queueInputs({ KEY_DOWN_R(INT_MENU_KEY), "sleep", KEY_UP_R(INT_MENU_KEY) }); }

void addKeybinds() { // Add keybinds here. Input syntax resembles AutoHotkey.

	new Keybind(BST_KEY, []() {
		prepareForIntMenuAndCacheLeftClickState();
		InputHandler::getInstance().queueInputs({ "enter downR", INT_MENU_KEY, "enter up", "enter downR",
								   "up 3", "enter up", "enter downR", "down down",
								   "enter upR", "down upR" });
		});

	new Keybind(THERMAL_KEY, []() {
		prepareForIntMenuAndCacheLeftClickState();
		InputHandler::getInstance().queueInputs(
			{ "enter downR", INT_MENU_KEY, "down 5", "enter up", "down downR",
			 "enter down", "down up", "enter upR", "sleep 2",
			 "space downR", INT_MENU_KEY_R, "space upR" });
		},
		{ "shift" });

	new Keybind(SNACKS_KEY, []() {
		prepareForIntMenuAndCacheLeftClickState();
		InputHandler::getInstance().queueInputs(
			{ INT_MENU_KEY_R, "enter down", "down 4", "enter up", "down downR",
			 "enter down", "down up", "down", "enter up" });
		},
		{ "shift" });

	new Keybind(AMMO_KEY, []() {

		/*
		POINT cursorPos;
		GetCursorPos(&cursorPos);
		InputHandler::Coordinates relativeCoords = InputHandler::getPixelCoordinatesReverse(cursorPos.x, cursorPos.y);
		std::cout << "Cursor normalized coordinates: (" << relativeCoords.x << ", " << relativeCoords.y << ")" << std::endl;
		std::cout << "Cursor actual coordinates: (" << cursorPos.x << ", " << cursorPos.y << ")" << std::endl;
		return;
		*/


#if USE_CURSOR_MACROS
		// Instead of queueing a mouse move, we can just force the cursor to be positioned where we want it to be. Prevents any movement by the user as well.
		InputHandler::getInstance().lockCursorTo(0.10625, 0.284259);
#if REPRESS_LEFT_CLICK
		bool leftClickPressed =
#endif
			prepareForIntMenuAndCacheLeftClickState();

		InputHandler::getInstance().queueInputs({ "enter downR", INT_MENU_KEY });
		// InputHandler::queueMouseMove(0.0911458, 0.234259, true);

		// For some reason, left clicking in 2 frames is quite inconsistent. You can make the int menu keypress recursive, but then it will sometimes shoot your weapon.
		InputHandler::getInstance().queueInputs({ "lbutton down", "sleep", "lbutton up", "enter up", "enter 2", "enter downR", "up down", "enter upR", "up upR", });
		ensureIntMenuClose();
		InputHandler::getInstance().queueTask(0, []() { InputHandler::getInstance().releaseCursor(); }, true);
#if REPRESS_LEFT_CLICK
		if (leftClickPressed) {
			InputHandler::getInstance().queueInputs({ "lbutton downR" });
		}
#endif
#else
		InputHandler::getInstance().queueInputs({ INT_MENU_KEY_R, "enter down", "down 4", "enter up", "enter 2", "enter downR", "up down", "enter upR", "up upR" });
		ensureIntMenuClose();
#endif
		});


	new Keybind(RPG_KEY, []() { InputHandler::getInstance().queueInputs({ KEY_DOWN(RPG_KEY), "tabR", KEY_UP(RPG_KEY) }); });
	new Keybind(STICKY_BOMB_KEY, []() { InputHandler::getInstance().queueInputs({ KEY_DOWN(STICKY_BOMB_KEY), "tabR", KEY_UP(STICKY_BOMB_KEY) }); });
	new Keybind(SNIPER_KEY, []() { InputHandler::getInstance().queueInputs({ KEY_DOWN(SNIPER_KEY), "tabR", KEY_UP(SNIPER_KEY) }); });
	new Keybind(WEAPON_KEY_1, []() { InputHandler::getInstance().queueInputs({ KEY_DOWN(WEAPON_KEY_1), "tabR", KEY_UP(WEAPON_KEY_1) }); });
	new Keybind(WEAPON_KEY_2, []() { InputHandler::getInstance().queueInputs({ KEY_DOWN(WEAPON_KEY_2), "tabR", KEY_UP(WEAPON_KEY_2) }); });
	new Keybind(WEAPON_KEY_3, []() { InputHandler::getInstance().queueInputs({ KEY_DOWN(WEAPON_KEY_3), "tabR", KEY_UP(WEAPON_KEY_3) }); });
	new Keybind(WEAPON_KEY_4, []() { InputHandler::getInstance().queueInputs({ KEY_DOWN(WEAPON_KEY_4), "tabR", KEY_UP(WEAPON_KEY_4) }); });
	new Keybind(WEAPON_KEY_5, []() { InputHandler::getInstance().queueInputs({ KEY_DOWN(WEAPON_KEY_5), "tabR", KEY_UP(WEAPON_KEY_5) }); });

	new Keybind(RPG_SPAM_KEY, []() { InputHandler::getInstance().queueInputs({ KEY_DOWN(STICKY_BOMB_KEY), "sleep 2", KEY_DOWN(RPG_KEY), "tabR", KEY_UP_R(RPG_KEY), KEY_UP_R(STICKY_BOMB_KEY) });});
	new Keybind(SNIPER_SPAM_KEY, []() { InputHandler::getInstance().queueInputs({ KEY_DOWN(STICKY_BOMB_KEY), KEY_DOWN(SNIPER_KEY), "tabR", KEY_UP_R(SNIPER_KEY), KEY_UP_R(STICKY_BOMB_KEY) }); });

	new Keybind(CHAT_KEYBIND, []() {
		if (RTSSReader::getInstance().targetProcess != "GTA5_Enhanced.exe") {
			inChat = true;
		}
		InputHandler::getInstance().queueInputs({ CHAT_KEYBIND });
		});

	new Keybind("esc", []() {
		inChat = false;
		InputHandler::getInstance().queueInputs({ "esc" });
		});

	new Keybind("enter", []() {
		inChat = false;
		InputHandler::getInstance().queueInputs({ "enter" });
		});

	/*
	Why is this so fucking inconsistent?
	new Keybind("F6", []() {
	  auto work_loop = [](auto &self) -> void {
		InputHandler::queueInputs(
			{"enter downR", "t", "hR", "eR", "lR", "lR", "o", "enter up"},
			[&self]() {
			  std::optional<WORD> keyCode = InputHandler::findKey("F6");

			  if (InputHandler::getInstance().getPhysicalKeyState(keyCode.value())) {

				printf("requeuing");
				InputHandler::getInstance().queueTask(0, [&self]() { self(self); }, true);
			  }
			});
	  };

	  work_loop(work_loop);
	});
	*/
}
bool isChatRelatedKey(DWORD vkCode) {
	return vkCode == InputHandler::getInstance().findKey(CHAT_KEYBIND).value() ||
		vkCode == VK_RETURN || vkCode == VK_ESCAPE;
}
BYTE keybindKeyState[] = { 0 };

LRESULT CALLBACK onKeyPress(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;

		// Check if the key event was injected (sent by SendInput() or something)
		// idfk how this works bro
		if (pKeyBoard->flags & LLKHF_INJECTED ||
			getActiveProcessName() != RTSSReader::getInstance().targetProcess) {
			return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
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
					if (!inChat || isChatRelatedKey(vkCode)) {
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
	return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK onMouseEvent(int nCode, WPARAM wParam, LPARAM lParam) {

	if (nCode == HC_ACTION) {
		MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;

		if ((pMouse->flags & LLMHF_INJECTED) ||
			getActiveProcessName() != RTSSReader::getInstance().targetProcess) {
			return CallNextHookEx(mouseHook, nCode, wParam, lParam);
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
					if (!inChat) {
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

uint64_t previousPresentTime = 0;
int frameGenMultiplier = 1; // For DLSS Frame Generation
int framesDetected = 0;
LARGE_INTEGER lastGenerated;
constexpr long long sleepTime = 0.5; // in ms

int main() {
	g_mainThreadId = GetCurrentThreadId();
	if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
		fprintf(stderr, "why cant i set priorirtyt fck bro");
		return 1;
	}
	addKeyboardHook();
	mouseHook = SetWindowsHookEx(WH_MOUSE_LL, onMouseEvent, GetModuleHandle(NULL), 0);

	if (keyboardHook == NULL || mouseHook == NULL) {
		fprintf(stderr, "why cant i install the hook");
		return 1;
	}
	RTSSReader::getInstance().initialize();
	addKeybinds();

	std::thread([]() {
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
		timeBeginPeriod(1);
		HANDLE hTimer = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (hTimer == NULL) {
			fprintf(stderr, "Failed to create high resolution timer.");
			exit(1);
		}
		while (true) {
			// qwPresentStartTime or qwPresentEndTime, haven't really tested which one is more consistent or if there's any difference at all.
			uint64_t presentTime = RTSSReader::getInstance().getAppMember<uint64_t>(offsetof(RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY, qwPresentEndTime));

			if (presentTime != previousPresentTime) {
				previousPresentTime = presentTime;
				if (RTSSReader::getInstance().targetProcess != "GTA5_Enhanced.exe" || ++framesDetected == frameGenMultiplier) {
					// LARGE_INTEGER currentTime, freq;
					// QueryPerformanceFrequency(&freq);
					// QueryPerformanceCounter(&currentTime);
					// printf("new frame, last frame was generated %fms ago\n",
					//        (currentTime.QuadPart - lastGenerated.QuadPart) * 1000.0 /
					//            freq.QuadPart);
					// QueryPerformanceCounter(&lastGenerated);
					framesDetected = 0;
					InputHandler::getInstance().executeFirstQueuedTask();
				}
			}

			// If there are currently queued tasks I want to check as often as
			// possible to check for FrameTime updates. This is incredibly bad for
			// the CPU but we should be doing it for very short time periods so it
			// should be OK.
			if (InputHandler::getInstance().queuedTasks.empty()) {
				if (InputHandler::getInstance().tasksPerformed > 0) {
					std::cout << "Frames taken to perform the task: " << InputHandler::getInstance().tasksPerformed << std::endl;
					InputHandler::getInstance().tasksPerformed = 0;
				}

				if (!RTSSReader::getInstance().isTargetAppStillRunning()) {
					removeKeyboardHook();
					std::cerr << "Target app is no longer running. Exiting..." << std::endl;
					MessageBoxA(NULL, "Target app is no longer running. Restart the macros because I can't be asked to write handling for this.", "Error", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND | MB_SYSTEMMODAL);
					exit(0);
				}
				LARGE_INTEGER dueTime;
				dueTime.QuadPart = -(sleepTime * 10000LL);

				if (SetWaitableTimer(hTimer, &dueTime, 0, NULL, NULL, FALSE)) { // This somehow lets me sleep with a precision of 0.5ms
					WaitForSingleObject(hTimer, INFINITE);
				}
			}
		}
		}).detach();

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		if (msg.message == WM_USER_REHOOK) {
			addKeyboardHook();
			continue;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	UnhookWindowsHookEx(keyboardHook);
	return 0;
}