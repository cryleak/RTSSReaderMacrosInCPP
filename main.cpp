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

#pragma comment(lib, "winmm.lib")
using namespace std::chrono_literals;

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
	DWORD frametimeMemoryOffset = 280;
	// DWORD framesGeneratedMemoryOffset = 332;
	HANDLE hMapFile;
	LPVOID pMapAddr;
	uintptr_t processEntryAddress;
	std::string targetProcess;

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
		const DWORD fileMapRead = 0x0004; // FILE_MAP_READ

		hMapFile = OpenFileMappingW(fileMapRead, FALSE, L"RTSSSharedMemoryV2");
		if (!hMapFile) {
			fprintf(stderr, "Could not open RTSS Shared Memory. Is "
				"RivaTuner Statistics Server running?");
			exit(1);
		}

		pMapAddr = MapViewOfFile(hMapFile, fileMapRead, 0, 0, 0);
		if (!pMapAddr) {
			CloseHandle(hMapFile);
			fprintf(stderr, "Failed to map view of shared memory.");
			exit(1);
		}
		processEntryAddress = 0;
	}

	std::optional<double> getRawFrametime() {
		char* base = static_cast<char*>(pMapAddr);

		if (processEntryAddress != 0) {
			DWORD rawFrametime =
				*reinterpret_cast<DWORD*>(processEntryAddress + frametimeMemoryOffset);
			return static_cast<double>(rawFrametime) / 1000.0;
		}
		else {
			DWORD dwAppEntrySize = *reinterpret_cast<DWORD*>(base + 8);
			DWORD dwAppArrOffset = *reinterpret_cast<DWORD*>(base + 12);
			DWORD dwAppArrSize = *reinterpret_cast<DWORD*>(base + 16);

			for (DWORD i = 0; i < dwAppArrSize; ++i) {
				uintptr_t entryBaseAddr = reinterpret_cast<uintptr_t>(base) +
					dwAppArrOffset + (i * dwAppEntrySize);

				char* appNamePtr = reinterpret_cast<char*>(entryBaseAddr + 4);
				std::string applicationName(appNamePtr);

				if (applicationName.find(targetProcess) != std::string::npos) {
					processEntryAddress = entryBaseAddr;

					DWORD rawFrametime = *reinterpret_cast<DWORD*>(processEntryAddress +
						frametimeMemoryOffset);
					return static_cast<double>(rawFrametime) / 1000.0;
				}
			}
		}
		return std::nullopt;
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
	void queueInputs(std::vector<std::string> inputs,
		std::function<void()> callback = nullptr);
	extern std::queue<Task> queuedTasks;
} // namespace InputHandler

class Keybind {
public:
	Keybind(int keyCode, std::function<void()> function,
		std::vector<std::string> modifiers = {}) {
		this->keyCode = keyCode;
		this->isPressed = false;
		this->modifiers = modifiers;
		this->function = [function]() {
			if (InputHandler::queuedTasks.empty()) {
				InputHandler::queueTask(0, function, false);
			}
			};
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
			SHORT vk = VkKeyScan(lowerCaseKey[0]);
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
		// add specific handling for mousewheel and mouse buttons cause i was really
		// lazy
		if (vkCode == VK_LBUTTON || vkCode == VK_RBUTTON || vkCode == VK_MBUTTON) {
			input.type = INPUT_MOUSE;
			input.mi.time = 0;
			input.mi.dwExtraInfo = 0;

			switch (vkCode) {
			case VK_LBUTTON:
				input.mi.dwFlags = pressDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
				break;
			case VK_RBUTTON:
				input.mi.dwFlags =
					pressDown ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
				break;
			case VK_MBUTTON:
				input.mi.dwFlags =
					pressDown ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
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
			SendInput(1, &input, sizeof(INPUT));
			input.mi.mouseData = 0;
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
					printf("%.3f sending %hu, state: %d\n",
						std::chrono::duration<double, std::milli>(
							std::chrono::steady_clock::now().time_since_epoch())
						.count(),
						vkCode, press);
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

				printf("Key code for '%s': %hd\n", input.c_str(), vkCode);
			}

			if (inputName == "wheelup" || inputName == "wheeldown") {
				queueInput(vkCode, true, false);
				queueTask(0, std::nullopt, false);
				continue;
			}

			// Schizo up and down logic because it is faster
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

	void queueMouseMove(Coordinates coords, bool recursive) {
		queueTask(0, [coords]() { moveToPixelCoordinates(coords.x, coords.y); }, recursive);
	}

	class TaskExecutor {
	public:
		~TaskExecutor() {
			if (worker.joinable()) {
				{
					std::unique_lock<std::mutex> lock(queue_mutex);
					stop_thread = true;
				}
				condition.notify_one();
				worker.join();
			}
		}

		void start() { worker = std::thread(&TaskExecutor::loop, this); }

		void enqueue(std::function<void()> task) {
			{
				std::unique_lock<std::mutex> lock(queue_mutex);
				if (stop_thread) {
					return;
				}
				tasks.push(std::move(task));
			}
			condition.notify_one();
		}

	private:
		void loop() {
			while (true) {
				std::function<void()> task;
				{
					std::unique_lock<std::mutex> lock(queue_mutex);
					condition.wait(lock, [this] { return !tasks.empty() || stop_thread; });

					if (stop_thread && tasks.empty()) {
						return;
					}

					task = std::move(tasks.front());
					tasks.pop();
				}

				task();
			}
		}

		std::thread worker;
		std::queue<std::function<void()>> tasks;
		std::mutex queue_mutex;
		std::condition_variable condition;
		bool stop_thread = false;
	};

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
				} else {
					break;
				}
			}
			if (firstTaskCopy.function.has_value()) {
				firstTaskCopy.function.value()();
			}
			if (!firstTaskCopy.recursive) {
				break;
			}
		}
	}

	void prepareForIntMenu() { queueInputs({ "lbutton upR", "rbutton upR" }); }

} // namespace InputHandler

bool inChat = false;

void addKeybinds() { // Add keybinds here
	// You can't type this keycode as a string so i just typed in the virtual
	// keycode of it instead
	new Keybind(220, []() {
		InputHandler::prepareForIntMenu();
		InputHandler::queueInputs({ "mR", "enter down", "enter up", "enter downR",
								   "up 3", "enter up", "enter downR", "down down",
								   "enter up", "down up" });
		});

	new Keybind(221,
		[]() {
			InputHandler::prepareForIntMenu();
			InputHandler::queueInputs(
				{ "mR", "enter down", "down 5", "enter up", "down downR",
				 "enter down", "down up", "enter upR", "sleep 2",
				 "space downR", "m down", "m upR", "space up" });
		},
		{ "shift" });

	new Keybind(186,
		[]() {
			InputHandler::prepareForIntMenu();
			InputHandler::queueInputs(
				{ "mR", "enter down", "down 4", "enter up", "down downR",
				 "enter down", "down up", "down", "enter up" });
		},
		{ "shift" });

	new Keybind("F2", []() {
		InputHandler::prepareForIntMenu();
		InputHandler::queueInputs({ "mR", "enter down", "down 4", "enter up",
								   "enter", "enter", "enter downR",
								   "up down", "enter up", "up up", "m" });
		});

	new Keybind("1",
		[]() { InputHandler::queueInputs({ "1 down", "tabR", "1 up" }); });
	new Keybind("2",
		[]() { InputHandler::queueInputs({ "2 down", "tabR", "2 up" }); });
	new Keybind("3",
		[]() { InputHandler::queueInputs({ "3 down", "tabR", "3 up" }); });
	new Keybind("4",
		[]() { InputHandler::queueInputs({ "4 down", "tabR", "4 up" }); });
	new Keybind("5",
		[]() { InputHandler::queueInputs({ "5 down", "tabR", "5 up" }); });
	new Keybind("6",
		[]() { InputHandler::queueInputs({ "6 down", "tabR", "6 up" }); });
	new Keybind("7",
		[]() { InputHandler::queueInputs({ "7 down", "tabR", "7 up" }); });
	new Keybind("8",
		[]() { InputHandler::queueInputs({ "8 down", "tabR", "8 up" }); });

	new Keybind("q", []() {
		InputHandler::queueInputs(
			{ "4 down", "1 down", "tabR", "1 upR", "4 upR" });
		});

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

HHOOK keyboardHook;
BYTE keybindKeyState[] = { 0 };

LRESULT CALLBACK onKeyPress(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HC_ACTION) {
		KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;

		// Check if the key event was injected (sent by SendInput() or something
		// idfk how this works bro
		if (pKeyBoard->flags & LLKHF_INJECTED ||
			getActiveProcessName() != RTSSReader::targetProcess) {
			return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
		}
		switch (wParam) {

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN: {
			// lParam is a pointer to a KBDLLHOOKSTRUCT
			KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;
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
			KBDLLHOOKSTRUCT* pKeyBoardUp = (KBDLLHOOKSTRUCT*)lParam;
			DWORD vkCode = pKeyBoardUp->vkCode;
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

double previousFrametime = 0;
int frameGenMultiplier = 1; // For DLSS Frame Generation
int framesDetected = 0;
static InputHandler::TaskExecutor taskExecutor;
LARGE_INTEGER lastGenerated;
constexpr long long sleepTime = 0.5; // in ms


int main() {
	if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
		fprintf(stderr, "why cant i set priorirtyt fck bro");
		return 1;
	}
	keyboardHook =
		SetWindowsHookEx(WH_KEYBOARD_LL, onKeyPress, GetModuleHandle(NULL), 0);

	if (keyboardHook == NULL) {
		fprintf(stderr, "why cant i install the hook");
		return 1;
	}
	RTSSReader::initialize();
	addKeybinds();

	std::thread([]() {
		taskExecutor.start();
		timeBeginPeriod(1);
		HANDLE hTimer = CreateWaitableTimerEx(
			NULL, NULL,
			CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
			TIMER_ALL_ACCESS
		);
		if (hTimer == NULL) {
			fprintf(stderr, "Failed to create high resolution timer.");
			exit(1);
		}
		while (true) {
			double frametime = RTSSReader::getRawFrametime().value_or(0);
			if (frametime !=
				previousFrametime) { // This doesn't work if you set an FPS cap
				// using RTSS but I couldn't find another way
				// to do it so fuck it, this literally relies
				// on frametime variance it's really funny
				previousFrametime = frametime;
				if (RTSSReader::targetProcess != "GTA5_Enhanced.exe" ||
					++framesDetected == frameGenMultiplier) {
					// LARGE_INTEGER currentTime, freq;
					// QueryPerformanceFrequency(&freq);
					// QueryPerformanceCounter(&currentTime);
					// printf("new frame, last frame was generated %fms ago\n",
					//        (currentTime.QuadPart - lastGenerated.QuadPart) * 1000.0 /
					//            freq.QuadPart);
					// QueryPerformanceCounter(&lastGenerated);
					framesDetected = 0;
					taskExecutor.enqueue(
						InputHandler::executeFirstQueuedTask); // Do this asynchronously
				}
			}

			// If there are currently queued tasks I want to check as often as
			// possible to check for FrameTime updates. This is incredibly bad for
			// the CPU but we should be doing it for very short time periods so it
			// should be OK.
			if (InputHandler::queuedTasks.empty()) {
				LARGE_INTEGER dueTime;
				dueTime.QuadPart = -(sleepTime * 10000LL);

				LARGE_INTEGER currentTime, freq, endTime;
				QueryPerformanceFrequency(&freq);
				QueryPerformanceCounter(&currentTime);
				if (SetWaitableTimer(hTimer, &dueTime, 0, NULL, NULL, FALSE)) { // This somehow lets me sleep with a precision of 0.5ms
					WaitForSingleObject(hTimer, INFINITE);
				}
				QueryPerformanceCounter(&endTime);
				// printf("new frame, last frame was generated %fms ago\n", (endTime.QuadPart - currentTime.QuadPart) * 1000.0 / freq.QuadPart);
			}
		}
		}).detach();

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	UnhookWindowsHookEx(keyboardHook);
	return 0;
}