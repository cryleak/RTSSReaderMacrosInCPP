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
#include "RTSSSharedMemory.h"

#pragma comment(lib, "winmm.lib")
using namespace std::chrono_literals;

#define WM_USER_REHOOK (WM_USER + 1)
#define USE_MOUSE_WHEEL 1

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

bool isProcessRunning(const TCHAR* processName) {
	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (Process32First(snapshot, &entry)) {
		while (Process32Next(snapshot, &entry)) {
			if (_tcscmp(entry.szExeFile, processName) == 0) {
				CloseHandle(snapshot);
				return true;
			}
		}
	}

	CloseHandle(snapshot);
	return false;
}

namespace RTSSReader {
	HANDLE hMapFile;
	RTSS_SHARED_MEMORY* pMapAddr;
	RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY* pTargetApp = nullptr;
	std::string targetProcess;

	bool findProcess(std::string name) {
		if (!pMapAddr) return false;
		char* appArray = (char*)pMapAddr + pMapAddr->dwAppArrOffset;

		for (DWORD i = 0; i < pMapAddr->dwAppArrSize; ++i) {
			auto entry = (RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY*)(appArray + (i * pMapAddr->dwAppEntrySize));
			if (std::string(entry->szName).find(name) != std::string::npos) {
				pTargetApp = entry;
				return true;
			}
		}
		return false;
	}

	bool isTargetAppStillRunning() {
		if (!pTargetApp) return true; // haven't found it yet so just assume it's still running I guess
		 
		DWORD pid = pTargetApp->dwProcessID;
		if (pid == 0) return false;

		static ULONGLONG lastKernelCheck = 0;
		ULONGLONG now = GetTickCount64();

		// Don't switch from user mode to kernel mode every time since it's quite slow and it doesn't matter to check this every single time we call this method, we can just check every second or so
		if (now - lastKernelCheck > 1000) {
			lastKernelCheck = now;
			HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			if (process) {
				DWORD exitCode;
				bool active = (GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE);
				CloseHandle(process);
				return active;
			}
			return false;
		}

		return true;
	}

	void initialize() {
		if (isProcessRunning(L"GTA5_Enhanced.exe")) {
			targetProcess = "GTA5_Enhanced.exe";
		}
		else if (isProcessRunning(L"GTA5.exe")) {
			targetProcess = "GTA5.exe";
		}
		else {
			fprintf(stderr, "Could not find GTA process. Is it running?");
			exit(1);
		}
		printf("%s\n", targetProcess.c_str());

		hMapFile = OpenFileMappingW(FILE_MAP_READ, FALSE, L"RTSSSharedMemoryV2");
		if (!hMapFile) {
			fprintf(stderr, "Could not open RTSS Shared Memory. Is RivaTuner Statistics Server running?");
			exit(1);
		}

		pMapAddr = reinterpret_cast<RTSS_SHARED_MEMORY*>(MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0));
		if (!pMapAddr) {
			CloseHandle(hMapFile);
			fprintf(stderr, "Failed to map view of shared memory.");
			exit(1);
		}
		if (!findProcess(targetProcess)) {
			std::cerr << "Failed to find target process in RTSS shared memory. Is the game running and being monitored by RTSS?";
			Sleep(2000);
			exit(1);
		}
	}


	template <typename T>
	T getAppMember(size_t offset) {
		if (!pTargetApp) return T();

		return *reinterpret_cast<T*>(reinterpret_cast<char*>(pTargetApp) + offset);
	}

} // namespace RTSSReader

namespace InputHandler {

	void queueTask(int delay, std::optional<std::function<void()>> function,
		bool recursive);
	std::optional<WORD> findKey(const std::string& keyToFind);

	struct Task {
		int delay;
		std::optional<std::function<void()>> function;
		bool recursive;
	};
	void queueTask(Task task);
	void queueInputs(std::vector<std::string> inputs, std::function<void()> callback = nullptr);
	extern std::queue<Task> queuedTasks;
} // namespace InputHandler

class Keybind {
public:
	Keybind(int keyCode, std::function<void()> exec,
		std::vector<std::string> modifiers = {}) {
		this->keyCode = keyCode;
		this->isPressed = false;
		this->modifiers = modifiers;
		this->function = exec;
		keybinds.push_back(*this);
	}

	Keybind(const std::string& key, std::function<void()> function,
		std::vector<std::string> modifiers = {})
		: Keybind(InputHandler::findKey(key).value(), function,
			modifiers) { // This should always have a value
	}

	static std::vector<Keybind> keybinds;
	bool isPressed;
	DWORD keyCode;
	std::function<void()> function;
	std::vector<std::string> modifiers;
};

std::vector<Keybind> Keybind::keybinds = {};

namespace InputHandler {

	int tasksPerformed = 0;
	static HKL usLayout = LoadKeyboardLayoutA("00000409", KLF_NOTELLSHELL);

	LPCTSTR GetCursorType() {
		HCURSOR current_cursor;
		CURSORINFO ci;
		ci.cbSize = sizeof(CURSORINFO);
		current_cursor = GetCursorInfo(&ci) ? ci.hCursor : NULL;

		if (!current_cursor)
		{
			return _T("Unknown");
		}

		static HCURSOR sCursor[] = {
			LoadCursor(NULL, IDC_APPSTARTING), LoadCursor(NULL, IDC_ARROW),
			LoadCursor(NULL, IDC_CROSS), LoadCursor(NULL, IDC_HELP),
			LoadCursor(NULL, IDC_IBEAM), LoadCursor(NULL, IDC_ICON),
			LoadCursor(NULL, IDC_NO), LoadCursor(NULL, IDC_SIZE),
			LoadCursor(NULL, IDC_SIZEALL), LoadCursor(NULL, IDC_SIZENESW),
			LoadCursor(NULL, IDC_SIZENS), LoadCursor(NULL, IDC_SIZENWSE),
			LoadCursor(NULL, IDC_SIZEWE), LoadCursor(NULL, IDC_UPARROW),
			LoadCursor(NULL, IDC_WAIT)
		};

		static const size_t cursor_count = sizeof(sCursor) / sizeof(sCursor[0]);

		static LPCTSTR sCursorName[cursor_count + 1] = {
			_T("AppStarting"), _T("Arrow"), _T("Cross"), _T("Help"),
			_T("IBeam"), _T("Icon"), _T("No"), _T("Size"),
			_T("SizeAll"), _T("SizeNESW"), _T("SizeNS"),
			_T("SizeNWSE"), _T("SizeWE"), _T("UpArrow"),
			_T("Wait"), _T("Unknown")
		};

		// Find matching cursor
		size_t i;
		for (i = 0; i < cursor_count; ++i)
			if (sCursor[i] == current_cursor)
				break;

		return sCursorName[i];
	}

	bool getPhysicalKeyState(WORD vkCode) {
		for (Keybind& keybind : Keybind::keybinds) {
			if (vkCode == keybind.keyCode) {
				return keybind.isPressed;
			}
		}
		// Return Windows API KeyState if there is no keybind with that keycode.
		return (GetAsyncKeyState(vkCode) & 0x8000) != 0;
	}

	std::optional<WORD> findKey(const std::string& keyToFind) {
		std::string lowerCaseKey = keyToFind;
		std::transform(lowerCaseKey.begin(), lowerCaseKey.end(), lowerCaseKey.begin(),
			[](unsigned char c) { return std::tolower(c); });

		std::optional<WORD> vkCode;
		for (size_t i = 0; i < g_key_to_vk_size; ++i) {
			if (g_key_to_vk[i].keyName == lowerCaseKey) {
				vkCode = g_key_to_vk[i].vkCode;
				break;
			}
		}

		if (!vkCode.has_value()) {
			SHORT vk = VkKeyScanExA(lowerCaseKey[0], usLayout);
			if (vk == -1) {
				printf("Failed to find keycode for: %s", lowerCaseKey.c_str());
				return std::nullopt;
			}
			vkCode = LOBYTE(vk);
		}
		return vkCode;
	}

	void sendKeyInput(WORD vkCode, bool pressDown) {
		INPUT input = { 0 };
		// add specific handling for mousewheel and mouse buttons cause i was really lazy
		if (vkCode == VK_LBUTTON || vkCode == VK_RBUTTON || vkCode == VK_MBUTTON || vkCode == VK_XBUTTON1 || vkCode == VK_XBUTTON2) {
			input.type = INPUT_MOUSE;
			input.mi.time = 0;
			input.mi.dwExtraInfo = 0;

			switch (vkCode) {
			case VK_LBUTTON:
				input.mi.dwFlags = pressDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
				break;
			case VK_RBUTTON:
				input.mi.dwFlags = pressDown ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
				break;
			case VK_MBUTTON:
				input.mi.dwFlags = pressDown ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
				break;
			case VK_XBUTTON1:
				input.mi.dwFlags = pressDown ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
				input.mi.mouseData = XBUTTON1;
				break;
			case VK_XBUTTON2:
				input.mi.dwFlags = pressDown ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
				input.mi.mouseData = XBUTTON2;
				break;
			}
			SendInput(1, &input, sizeof(INPUT));
		}
		else if (vkCode == 0x1001 || vkCode == 0x1000) {
			input.type = INPUT_MOUSE;
			input.mi.dwFlags = MOUSEEVENTF_WHEEL;
			input.mi.mouseData = (vkCode == 0x1001) ? WHEEL_DELTA : -WHEEL_DELTA;
			input.mi.time = 0;
			input.mi.dwExtraInfo = 0;
			input.mi.dx = 0;
			input.mi.dy = 0;
			SendInput(1, &input, sizeof(INPUT));
		}
		else {
			input.type = INPUT_KEYBOARD;

			input.ki.wVk = vkCode;
			input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
			input.ki.time = 0;
			input.ki.dwExtraInfo = 0;
			input.ki.dwFlags = KEYEVENTF_SCANCODE;

			switch (
				vkCode) { // For some keycodes you need to add this flag or something
			case VK_UP:
			case VK_DOWN:
			case VK_LEFT:
			case VK_RIGHT:
			case VK_HOME:
			case VK_END:
			case VK_PRIOR:
			case VK_NEXT:
			case VK_INSERT:
			case VK_DELETE:
			case VK_LCONTROL:
			case VK_RCONTROL:
			case VK_LSHIFT:
			case VK_RSHIFT:
			case VK_LMENU:
			case VK_RMENU:
			case VK_APPS:
				input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
				break;
			}

			if (!pressDown) {
				input.ki.dwFlags |= KEYEVENTF_KEYUP;
			}
			SendInput(1, &input, sizeof(INPUT));
		}
	}

	std::queue<Task> queuedTasks = {};
	std::mutex queuedTasksMutex;

	void queueTask(Task task) {
		std::lock_guard<std::mutex> lock(queuedTasksMutex);
		queuedTasks.push(task);
	}

	void queueTask(int delay, std::optional<std::function<void()>> function,
		bool recursive) {
		std::lock_guard<std::mutex> lock(queuedTasksMutex);
		queuedTasks.push({ delay, function, recursive });
	}

	void queueInput(WORD vkCode, std::optional<bool> state, bool recursive) {
		auto enqueue = [&](bool press, bool recursiveInput) {
			queueTask(
				0,
				[vkCode, press]() {
					sendKeyInput(vkCode, press);
					// printf("%.3f sending %hu, state: %d\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(), vkCode, press);
				},
				recursiveInput);
			};

		if (state.has_value()) {
			enqueue(state.value(), recursive);
		}
		else {
			enqueue(true, false);
			enqueue(false, recursive);
		}
	}

	std::regex inputPattern(R"((\w+?)(?:\s(down|up|\d+))?(R)?)");
	void queueInputs(std::vector<std::string> inputs,
		std::function<void()> callback) {
		for (size_t i = 0; i < inputs.size(); ++i) {
			const std::string& input = inputs[i];
			std::smatch matches;
			if (!std::regex_match(input, matches, inputPattern)) {
				return;
			}
			std::string inputName = matches[1];
			std::string secondArg = matches[2];
			bool isRecursive = matches[3].matched;

			std::optional<bool> state = std::nullopt;
			int amount = 1;
			if (matches[2].matched) {
				if (secondArg == "down") {
					state = true;
				}
				else if (secondArg == "up") {
					state = false;
				}
				else if (!secondArg.empty() &&
					std::all_of(secondArg.begin(), secondArg.end(), ::isdigit)) {
					amount = std::stoi(secondArg);
				}
			}

			WORD vkCode;
			if (inputName == "sleep") {
				for (int i = 0; i < amount; i++) {
					queueTask(0, std::nullopt, isRecursive);
				}
				continue;
			}
			else {
				std::optional<WORD> keyOpt = findKey(inputName);
				if (!keyOpt.has_value()) {
					return;
				}
				vkCode = keyOpt.value();

				// printf("Key code for '%s': %hd\n", input.c_str(), vkCode);
			}

			if (inputName == "wheelup" || inputName == "wheeldown") {
				queueInput(vkCode, true, false);
				queueTask(0, std::nullopt, false);
				continue;
			}

			// Schizo up and down logic because it is faster
#if USE_MOUSE_WHEEL
			if (_tcscmp(GetCursorType(), _T("Unknown")) == 0) {
				if ((inputName == "up" || inputName == "down") && amount != 1 &&
					!state.has_value()) {
					WORD wheelInput = findKey("wheel" + inputName).value();
					for (int i = 0; i < floor(amount / 2); i++) {
						queueInput(vkCode, true, false);
						queueInput(vkCode, false, true);
						queueInput(wheelInput, false, false);
						if (amount >= 3) {
							queueTask(0, std::nullopt, false);
						}
					}
					if (amount & 1) {
						queueInput(vkCode, true, false);
						queueInput(vkCode, false, true);
					}
					continue;
				}
			}
#endif


			for (int i = 0; i < amount; i++) {
				queueInput(vkCode, state, isRecursive);
			}
		}
		if (callback) {
			queueTask(0, callback, true);
		}
	}

	class Coordinates {
	public:
		Coordinates(double x, double y) {
			this->x = x;
			this->y = y;
		}
		double x;
		double y;
	};

	// Get the coordinates on the main screen for a certain x and y from 0 to 1
	Coordinates getPixelCoordinates(double x, double y) {
		DEVMODE devMode;
		devMode.dmSize = sizeof(DEVMODE);
		EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &devMode);

		int screenWidth = devMode.dmPelsWidth;
		int screenHeight = devMode.dmPelsHeight;

		double widescreenWidth = screenHeight * (16.0 / 9.0);
		double offsetX = (screenWidth - widescreenWidth) / 2.0;

		double pixelX = offsetX + (widescreenWidth * x);
		double pixelY = screenHeight * y;

		Coordinates pixelCoords = Coordinates(pixelX, pixelY);

		return pixelCoords;
	}

	// Convert pixel coordinates back to normalized coordinates (0 to 1)
	Coordinates getPixelCoordinatesReverse(double pixelX, double pixelY) {
		DEVMODE devMode;
		devMode.dmSize = sizeof(DEVMODE);
		EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &devMode);

		int screenWidth = devMode.dmPelsWidth;
		int screenHeight = devMode.dmPelsHeight;

		double widescreenWidth = screenHeight * (16.0 / 9.0);
		double offsetX = (screenWidth - widescreenWidth) / 2.0;

		double x = (pixelX - offsetX) / widescreenWidth;
		double y = pixelY / screenHeight;

		Coordinates coordPercentages = Coordinates(x, y);

		return coordPercentages;
	}

	// Move mouse cursor to normalized coordinates (0 to 1)
	void moveToPixelCoordinates(double x, double y) {
		Coordinates coords = getPixelCoordinates(x, y);
		SetCursorPos(static_cast<int>(coords.x), static_cast<int>(coords.y));
		std::cout << "Moving cursor to normalized coordinates: (" << x << ", " << y << ")" << std::endl;
		std::cout << "Moving cursor to pixel coordinates: (" << coords.x << ", " << coords.y << ")" << std::endl;
	}

	void queueMouseMove(double x, double y, bool recursive) {
		queueTask(0, [x, y]() { moveToPixelCoordinates(x, y); }, recursive);
	}

	void lockCursorTo(double x, double y) {
		Coordinates coords = getPixelCoordinates(x, y);
		POINT p = { coords.x, coords.y };

		RECT rect;
		rect.left = p.x;
		rect.right = p.x;
		rect.top = p.y;
		rect.bottom = p.y;
		ClipCursor(&rect);
	}

	void releaseCursor() {
		ClipCursor(NULL);
	}


	void executeFirstQueuedTask() {
		while (true) {
			Task firstTaskCopy;
			{
				std::lock_guard<std::mutex> lock(queuedTasksMutex);
				if (queuedTasks.empty()) {
					break;
				}
				Task& firstTaskReference = queuedTasks.front();
				if (--firstTaskReference.delay < 0) {
					firstTaskCopy = firstTaskReference;
					queuedTasks.pop();
				}
				else {
					break;
				}
			}
			if (firstTaskCopy.function.has_value()) {
				firstTaskCopy.function.value()();
			}
			if (!firstTaskCopy.recursive) {
				tasksPerformed++; // amount of frames done
				break;
			}
		}
	}


} // namespace InputHandler

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

// macro keybinds
#define BST_KEY 220
#define THERMAL_KEY 221
#define SNACKS_KEY 186
#define AMMO_KEY "F2"
#define RPG_SPAM_KEY "f24" // i don't use this
#define SNIPER_SPAM_KEY "q"

bool prepareForIntMenuAndCacheLeftClickState() {
	InputHandler::queueInputs({ "lbutton upR", "rbutton upR" });
	return InputHandler::getPhysicalKeyState(InputHandler::findKey("lbutton").value());
}
void ensureIntMenuClose() { InputHandler::queueInputs({ KEY_DOWN_R(INT_MENU_KEY), "sleep", KEY_UP_R(INT_MENU_KEY) }); }


void addKeybinds() { // Add keybinds here. Input syntax resembles AutoHotkey.

	new Keybind(BST_KEY, []() {
		prepareForIntMenuAndCacheLeftClickState();
		InputHandler::queueInputs({ "enter downR", INT_MENU_KEY, "enter up", "enter downR",
								   "up 3", "enter up", "enter downR", "down down",
								   "enter upR", "down upR" });
		});

	new Keybind(THERMAL_KEY, []() {
		prepareForIntMenuAndCacheLeftClickState();
		InputHandler::queueInputs(
			{ "enter downR", INT_MENU_KEY, "down 5", "enter up", "down downR",
			 "enter down", "down up", "enter upR", "sleep 2",
			 "space downR", INT_MENU_KEY_R, "space upR" });
		},
		{ "shift" });

	new Keybind(SNACKS_KEY, []() {
		prepareForIntMenuAndCacheLeftClickState();
		InputHandler::queueInputs(
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
		InputHandler::lockCursorTo(0.10625, 0.284259);
#if REPRESS_LEFT_CLICK
		bool leftClickPressed =
#endif
			prepareForIntMenuAndCacheLeftClickState();

		InputHandler::queueInputs({ "enter downR", INT_MENU_KEY});
		// InputHandler::queueMouseMove(0.0911458, 0.234259, true);

		// For some reason, left clicking in 2 frames is quite inconsistent. You can make the int menu keypress recursive, but then it will sometimes shoot your weapon.
		InputHandler::queueInputs({ "lbutton down", "sleep", "lbutton up", "enter up", "enter 2", "enter downR", "up down", "enter upR", "up upR",});
		ensureIntMenuClose();
		InputHandler::queueTask(0, []() { InputHandler::releaseCursor(); }, true);
#if REPRESS_LEFT_CLICK
		if (leftClickPressed) {
			InputHandler::queueInputs({ "lbutton downR" });
		}
#endif
#else
		InputHandler::queueInputs({ INT_MENU_KEY_R, "enter down", "down 4", "enter up", "enter 2", "enter downR", "up down", "enter upR", "up upR" });
		ensureIntMenuClose();
#endif
		});


	new Keybind(RPG_KEY, []() { InputHandler::queueInputs({ KEY_DOWN(RPG_KEY), "tabR", KEY_UP(RPG_KEY) }); });
	new Keybind(STICKY_BOMB_KEY, []() { InputHandler::queueInputs({ KEY_DOWN(STICKY_BOMB_KEY), "tabR", KEY_UP(STICKY_BOMB_KEY) }); });
	new Keybind(SNIPER_KEY, []() { InputHandler::queueInputs({ KEY_DOWN(SNIPER_KEY), "tabR", KEY_UP(SNIPER_KEY) }); });
	new Keybind(WEAPON_KEY_1, []() { InputHandler::queueInputs({ KEY_DOWN(WEAPON_KEY_1), "tabR", KEY_UP(WEAPON_KEY_1) }); });
	new Keybind(WEAPON_KEY_2, []() { InputHandler::queueInputs({ KEY_DOWN(WEAPON_KEY_2), "tabR", KEY_UP(WEAPON_KEY_2) }); });
	new Keybind(WEAPON_KEY_3, []() { InputHandler::queueInputs({ KEY_DOWN(WEAPON_KEY_3), "tabR", KEY_UP(WEAPON_KEY_3) }); });
	new Keybind(WEAPON_KEY_4, []() { InputHandler::queueInputs({ KEY_DOWN(WEAPON_KEY_4), "tabR", KEY_UP(WEAPON_KEY_4) }); });
	new Keybind(WEAPON_KEY_5, []() { InputHandler::queueInputs({ KEY_DOWN(WEAPON_KEY_5), "tabR", KEY_UP(WEAPON_KEY_5) }); });

	new Keybind(RPG_SPAM_KEY, []() { InputHandler::queueInputs({ KEY_DOWN(STICKY_BOMB_KEY), "sleep 2", KEY_DOWN(RPG_KEY), "tabR", KEY_UP_R(RPG_KEY), KEY_UP_R(STICKY_BOMB_KEY) });});
	new Keybind(SNIPER_SPAM_KEY, []() { InputHandler::queueInputs({ KEY_DOWN(STICKY_BOMB_KEY), KEY_DOWN(SNIPER_KEY), "tabR", KEY_UP_R(SNIPER_KEY), KEY_UP_R(STICKY_BOMB_KEY) }); });

	/*
	Why is this so fucking inconsistent?
	new Keybind("F6", []() {
	  auto work_loop = [](auto &self) -> void {
		InputHandler::queueInputs(
			{"enter downR", "t", "hR", "eR", "lR", "lR", "o", "enter up"},
			[&self]() {
			  std::optional<WORD> keyCode = InputHandler::findKey("F6");

			  if (InputHandler::getPhysicalKeyState(keyCode.value())) {

				printf("requeuing");
				InputHandler::queueTask(0, [&self]() { self(self); }, true);
			  }
			});
	  };

	  work_loop(work_loop);
	});
	*/
}
BYTE keybindKeyState[] = { 0 };

LRESULT CALLBACK onKeyPress(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;

		// Check if the key event was injected (sent by SendInput() or something)
		// idfk how this works bro
		if (pKeyBoard->flags & LLKHF_INJECTED ||
			getActiveProcessName() != RTSSReader::targetProcess) {
			return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
		}
		switch (wParam) {

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN: {
			// lParam is a pointer to a KBDLLHOOKSTRUCT
			DWORD vkCode = pKeyBoard->vkCode;

			for (Keybind& keybind : Keybind::keybinds) {
				bool modifiersPressed = keybind.modifiers.size() != 0 ? std::all_of(keybind.modifiers.begin(), keybind.modifiers.end(), [](std::string modifier) {
					std::optional<WORD> key = InputHandler::findKey(modifier);
					return InputHandler::getPhysicalKeyState(key.value());
					}) : true;
				if (vkCode == keybind.keyCode && !keybind.isPressed && modifiersPressed) {
					keybind.isPressed = true;
					if (!inChat) {
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
			getActiveProcessName() != RTSSReader::targetProcess) {
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
					std::optional<WORD> key = InputHandler::findKey(modifier);
					return InputHandler::getPhysicalKeyState(key.value());
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
	RTSSReader::initialize();
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
			uint64_t presentTime = RTSSReader::getAppMember<uint64_t>(offsetof(RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY, qwPresentEndTime));

			if (presentTime != previousPresentTime) {
				previousPresentTime = presentTime;
				if (RTSSReader::targetProcess != "GTA5_Enhanced.exe" || ++framesDetected == frameGenMultiplier) {
					// LARGE_INTEGER currentTime, freq;
					// QueryPerformanceFrequency(&freq);
					// QueryPerformanceCounter(&currentTime);
					// printf("new frame, last frame was generated %fms ago\n",
					//        (currentTime.QuadPart - lastGenerated.QuadPart) * 1000.0 /
					//            freq.QuadPart);
					// QueryPerformanceCounter(&lastGenerated);
					framesDetected = 0;
					InputHandler::executeFirstQueuedTask();
				}
			}

			// If there are currently queued tasks I want to check as often as
			// possible to check for FrameTime updates. This is incredibly bad for
			// the CPU but we should be doing it for very short time periods so it
			// should be OK.
			if (InputHandler::queuedTasks.empty()) {
				if (InputHandler::tasksPerformed > 0) {
					std::cout << "Frames taken to perform the task: " << InputHandler::tasksPerformed << std::endl;
					InputHandler::tasksPerformed = 0;
				}

				if (!RTSSReader::isTargetAppStillRunning()) {
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