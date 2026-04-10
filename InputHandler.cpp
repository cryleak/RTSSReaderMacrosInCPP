#include "InputHandler.h"
#include "Keybind.h"
#include "keymap.h"
#include <iostream>
#include <algorithm>
#include "KeyboardHookHandler.h"
#include <iomanip>

InputHandler::InputHandler()
	: tasksPerformed(0),
	inputPattern(R"((\w+?)(?:\s(down|up|\d+))?(R)?)") {
	usLayout = LoadKeyboardLayoutA("00000409", KLF_NOTELLSHELL);
}

LPCTSTR InputHandler::GetCursorType() {
	CURSORINFO ci = { sizeof(CURSORINFO) };
	HCURSOR current_cursor = GetCursorInfo(&ci) ? ci.hCursor : NULL;
	if (!current_cursor) return _T("Unknown");

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

	const size_t cursor_count = sizeof(sCursor) / sizeof(sCursor[0]);
	static LPCTSTR sCursorName[] = {
		_T("AppStarting"), _T("Arrow"), _T("Cross"), _T("Help"),
		_T("IBeam"), _T("Icon"), _T("No"), _T("Size"),
		_T("SizeAll"), _T("SizeNESW"), _T("SizeNS"),
		_T("SizeNWSE"), _T("SizeWE"), _T("UpArrow"),
		_T("Wait"), _T("Unknown")
	};

	size_t i;
	for (i = 0; i < cursor_count; ++i)
		if (sCursor[i] == current_cursor) break;

	return sCursorName[i];
}

bool InputHandler::getPhysicalKeyState(WORD vkCode) {
	for (Keybind& keybind : Keybind::keybinds) {
		if (vkCode == keybind.keyCode) return keybind.isPressed;
	}
	return (GetAsyncKeyState(vkCode) & 0x8000) != 0;
}

std::optional<WORD> InputHandler::findKey(const std::string& keyToFind) {
	std::string lowerCaseKey = keyToFind;
	std::transform(lowerCaseKey.begin(), lowerCaseKey.end(), lowerCaseKey.begin(), ::tolower);

	for (size_t i = 0; i < g_key_to_vk_size; ++i) {
		if (g_key_to_vk[i].keyName == lowerCaseKey) return g_key_to_vk[i].vkCode;
	}

	SHORT vk = VkKeyScanExA(lowerCaseKey[0], usLayout);
	if (vk == -1) return std::nullopt;
	return (WORD)LOBYTE(vk);
}

void InputHandler::sendKeyInput(WORD vkCode, bool pressDown) {
	INPUT input = { 0 };
	if (vkCode >= VK_LBUTTON && vkCode <= VK_XBUTTON2) {
		input.type = INPUT_MOUSE;
		switch (vkCode) {
		case VK_LBUTTON:  input.mi.dwFlags = pressDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
		case VK_RBUTTON:  input.mi.dwFlags = pressDown ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
		case VK_MBUTTON:  input.mi.dwFlags = pressDown ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
		case VK_XBUTTON1:
			input.mi.dwFlags = pressDown ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
			input.mi.mouseData = XBUTTON1; break;
		case VK_XBUTTON2:
			input.mi.dwFlags = pressDown ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
			input.mi.mouseData = XBUTTON2; break;
		}
	}
	else if (vkCode == 0x1001 || vkCode == 0x1000) {
		input.type = INPUT_MOUSE;
		input.mi.dwFlags = MOUSEEVENTF_WHEEL;
		input.mi.mouseData = (vkCode == 0x1001) ? WHEEL_DELTA : -WHEEL_DELTA;
	}
	else {
		input.type = INPUT_KEYBOARD;
		input.ki.wVk = vkCode;
		input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
		input.ki.dwFlags = KEYEVENTF_SCANCODE | (pressDown ? 0 : KEYEVENTF_KEYUP);

		// Extended key logic
		if (vkCode == VK_UP || vkCode == VK_DOWN || vkCode == VK_LEFT || vkCode == VK_RIGHT ||
			vkCode == VK_LCONTROL || vkCode == VK_RCONTROL || vkCode == VK_INSERT || vkCode == VK_DELETE) {
			input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
		}
	}
	SendInput(1, &input, sizeof(INPUT));
}

void InputHandler::queueTask(Task task) {
	std::lock_guard<std::mutex> lock(queuedTasksMutex);
	queuedTasks.push(task);
}

void InputHandler::queueTask(int delay, std::optional<std::function<void()>> function, bool recursive) {
	std::lock_guard<std::mutex> lock(queuedTasksMutex);
	queuedTasks.push({ delay, function, recursive });
}

void InputHandler::queueInput(WORD vkCode, std::optional<bool> state, bool recursive) {
	auto enqueue = [&](bool press, bool rec) {
		queueTask(0, [this, vkCode, press]() { this->sendKeyInput(vkCode, press); }, rec);
		};
	if (state.has_value()) enqueue(state.value(), recursive);
	else { enqueue(true, false); enqueue(false, recursive); }
}

void InputHandler::queueInputs(std::vector<std::string> inputs, std::function<void()> callback) {
	for (const std::string& input : inputs) {
		std::smatch matches;
		if (!std::regex_match(input, matches, inputPattern)) continue;

		std::string inputName = matches[1];
		bool isRecursive = matches[3].matched;
		int amount = matches[2].matched && std::isdigit(matches[2].str()[0]) ? std::stoi(matches[2].str()) : 1;

		std::optional<bool> state;
		if (matches[2].str() == "down") state = true;
		else if (matches[2].str() == "up") state = false;

		if (inputName == "sleep") {
			for (int i = 0; i < amount; i++) queueTask(0, std::nullopt, isRecursive);
			continue;
		}

		auto keyOpt = findKey(inputName);
		if (!keyOpt) continue;
		WORD vkCode = *keyOpt;

		for (int i = 0; i < amount; i++) queueInput(vkCode, state, isRecursive);
	}
	if (callback) queueTask(0, callback, true);
}

void InputHandler::executeFirstQueuedTask() {
	while (true) {
		Task taskToRun;
		bool hasTask = false;
		{
			std::lock_guard<std::mutex> lock(queuedTasksMutex);
			if (queuedTasks.empty()) break;

			if (--queuedTasks.front().delay < 0) {
				taskToRun = queuedTasks.front();
				queuedTasks.pop();
				hasTask = true;
			}
			else break;
		}

		if (hasTask) {
			if (taskToRun.function) (*taskToRun.function)();
			if (!taskToRun.recursive) {
				tasksPerformed++;
				break;
			}
		}
	}
}

void InputHandler::lockCursorTo(double x, double y) {
	Coordinates coords = getPixelCoordinates(x, y);
	RECT rect = { (long)coords.x, (long)coords.y, (long)coords.x, (long)coords.y };
	ClipCursor(&rect);
}

void InputHandler::releaseCursor() { ClipCursor(NULL); }

InputHandler::Coordinates InputHandler::getPixelCoordinates(double x, double y) {
	DEVMODE devMode = { 0 };
	devMode.dmSize = sizeof(DEVMODE);
	EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &devMode);
	double widescreenWidth = devMode.dmPelsHeight * (16.0 / 9.0);
	double offsetX = (devMode.dmPelsWidth - widescreenWidth) / 2.0;
	return Coordinates(offsetX + (widescreenWidth * x), devMode.dmPelsHeight * y);
}