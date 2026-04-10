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
#include "HookHandler.h"
#include "Utils.h"

#pragma comment(lib, "winmm.lib")
using namespace std::chrono_literals;

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

// macro keybinds
#define BST_KEY 220
#define THERMAL_KEY 221
#define SNACKS_KEY 186
#define AMMO_KEY "F2"
#define RPG_SPAM_KEY "f24" // i don't use this
#define SNIPER_SPAM_KEY "f24"
#define DOUBLE_SWITCH_KEY "q"

bool prepareForIntMenuAndCacheLeftClickState() {
	InputHandler::getInstance().queueInputs({ "lbutton upR", "rbutton upR" });
	return InputHandler::getInstance().getPhysicalKeyState(InputHandler::getInstance().findKey("lbutton").value());
}

// Must manually release int menu key later
void ensureIntMenuOpen() { InputHandler::getInstance().queueInputs({ KEY_DOWN(INT_MENU_KEY), "sleep" }); }

void ensureIntMenuClose() { InputHandler::getInstance().queueInputs({ KEY_DOWN(INT_MENU_KEY), "sleep", KEY_UP_R(INT_MENU_KEY) }); }

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
		// InputHandler::getInstance().lockCursorTo(0.10625, 0.284259);
		InputHandler::getInstance().lockCursorTo(0.0911458, 0.234259);
#if REPRESS_LEFT_CLICK
		bool leftClickPressed =
#endif
			prepareForIntMenuAndCacheLeftClickState();

		InputHandler::getInstance().queueInputs({ "enter downR" });
		ensureIntMenuOpen();
		InputHandler::getInstance().queueInputs({ "sleep" });
		// InputHandler::queueMouseMove(0.0911458, 0.234259, true);

		// For some reason, left clicking in 2 frames is quite inconsistent. You can make the int menu keypress recursive, but then it will sometimes shoot your weapon.
		InputHandler::getInstance().queueInputs({ "lbutton down", KEY_UP(INT_MENU_KEY), "lbutton up", "enter up", "enter 2", "enter downR", "up down", "enter upR", "up upR", });
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
	new Keybind(DOUBLE_SWITCH_KEY, []() { InputHandler::getInstance().queueInputs({ RPG_KEY, KEY_DOWN(RPG_KEY), "tabR", KEY_UP_R(RPG_KEY) }); });

	if (RTSSReader::getInstance().targetProcess != "GTA5_Enhanced.exe") {
		new Keybind(CHAT_KEYBIND, []() {
			HookHandler::getInstance().inChat = true;
			InputHandler::getInstance().queueInputs({ CHAT_KEYBIND });
			});

		new Keybind("esc", []() {
			HookHandler::getInstance().inChat = false;
			InputHandler::getInstance().queueInputs({ "esc" });
			});

		new Keybind("enter", []() {
			HookHandler::getInstance().inChat = false;
			InputHandler::getInstance().queueInputs({ "enter" });
			});
	}

	/*
	Why is this so fucking inconsistent?
	new Keybind("F6", []() {
	  auto work_loop = [](auto &self) -> void {
		InputHandler::queueInputs(
			{"enter downR", "t", "hR", "eR", "lR", "lR", "o", "enter up"},
			[&self]() {
			  std::optional<WORD> keyCode = InputHandler::findKey("F6");

			  if (InputHandler::getInstance().getPhysicalKeyState(keyCode.value())) {

				InputHandler::getInstance().queueTask(0, [&self]() { self(self); }, true);
			  }
			});
	  };

	  work_loop(work_loop);
	});
	*/
}
BYTE keybindKeyState[] = { 0 };

uint64_t previousPresentTime = 0;
int frameGenMultiplier = 1; // For DLSS Frame Generation
int framesDetected = 0;
LARGE_INTEGER lastGenerated;
constexpr long long sleepTime = 0.5; // in ms

int main() {
	if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
		std::cerr << "Failed to set process priority." << std::endl;
		return 1;
	}

	if (!HookHandler::getInstance().addKeyboardHook() || !HookHandler::getInstance().addMouseHook()) {
		std::cerr << "Failed to install the hook." << std::endl;
		return 1;
	}
	RTSSReader::getInstance(); // initalize
	addKeybinds();
	std::cout << "Keybinds initalized successfully." << std::endl;
	if (!SetProcessPriorityByName(std::wstring(RTSSReader::getInstance().targetProcess.begin(), RTSSReader::getInstance().targetProcess.end()), HIGH_PRIORITY_CLASS)) {
		std::cerr << "Failed to set process priority of " << RTSSReader::getInstance().targetProcess << "." << std::endl;
		return 1;
	}
	std::cout << "GTA's process priority set successfully." << std::endl;

	std::thread([]() {
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
		timeBeginPeriod(1);
		HANDLE hTimer = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (hTimer == NULL) {
			std::cerr << "Failed to create high resolution timer." << std::endl;
			exit(1);
		}
		static ULONGLONG lastHookTime = 0;
		while (true) {
			// qwPresentStartTime or qwPresentEndTime, haven't really tested which one is more consistent or if there's any difference at all.
			uint64_t presentTime = RTSSReader::getInstance().getAppMember<uint64_t>(offsetof(RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY, qwPresentEndTime));
			// std::cout << "Present time: " << presentTime << std::endl;
			if (presentTime != previousPresentTime) {
				previousPresentTime = presentTime;
				if (RTSSReader::getInstance().targetProcess != "GTA5_Enhanced.exe" || ++framesDetected == frameGenMultiplier) {
					framesDetected = 0;
					InputHandler::getInstance().executeFirstQueuedTask();
				}
			}

			// If there are currently queued tasks I want to check as often as possible for present time updates. This is incredibly bad for the CPU but we should be doing it for very short time periods so it should be OK.
			if (InputHandler::getInstance().queuedTasks.empty()) {
				if (InputHandler::getInstance().tasksPerformed > 0) {
					auto keybindStartTime = Keybind::keybindStartTime;
					auto timeSinceKeybindStart = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count() - keybindStartTime;
					std::cout << "Frames taken to perform the task: " << InputHandler::getInstance().tasksPerformed << " (" << timeSinceKeybindStart << "ms taken to perform)" << std::endl;
					InputHandler::getInstance().tasksPerformed = 0;

					lastHookTime = GetTickCount64();
					HookHandler::getInstance().rehook();
				}

				if (!RTSSReader::getInstance().isTargetAppStillRunning()) {
					HookHandler::getInstance().removeKeyboardHook();
					HookHandler::getInstance().removeMouseHook();
					std::cerr << "Target app is no longer running. Exiting..." << std::endl;
					MessageBoxA(NULL, "Target app is no longer running. Restart the macros because I can't be asked to write handling for this.", "Error", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND | MB_SYSTEMMODAL);
					exit(0);
				}

				if (GetTickCount64() - lastHookTime >= 5000) { // Reinstall keyboard hook every 5 seconds or when tasks are finished. 
					lastHookTime = GetTickCount64();
					HookHandler::getInstance().rehook();
				}

				LARGE_INTEGER dueTime;
				dueTime.QuadPart = -(sleepTime * 10000LL);

				if (SetWaitableTimer(hTimer, &dueTime, 0, NULL, NULL, FALSE)) { // This somehow lets me sleep with a precision of 0.5ms
					WaitForSingleObject(hTimer, INFINITE);
				}
			}
		}
		}).detach();

	std::cout << "Initialization complete." << std::endl;

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		if (msg.message == WM_USER_REHOOK_KEYBOARD) {
			HookHandler::getInstance().addKeyboardHook();
			continue;
		} else if (msg.message == WM_USER_REHOOK_MOUSE) {
			HookHandler::getInstance().addMouseHook();
			continue;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}