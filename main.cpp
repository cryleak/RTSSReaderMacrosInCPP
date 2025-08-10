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

bool isProcessRunning(const TCHAR *processName) {
  PROCESSENTRY32 entry;
  entry.dwSize = sizeof(PROCESSENTRY32);

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

  if (Process32First(snapshot, &entry) == TRUE) {
    while (Process32Next(snapshot, &entry) == TRUE) {
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
  targetProcess =
      isProcessRunning("GTA5_Enhanced.exe") ? "GTA5_Enhanced.exe" : "GTA5.exe";
  printf("%s", targetProcess.c_str());
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
  char *base = static_cast<char *>(pMapAddr);

  if (processEntryAddress != 0) {
    DWORD rawFrametime =
        *reinterpret_cast<DWORD *>(processEntryAddress + frametimeMemoryOffset);
    return static_cast<double>(rawFrametime) / 1000.0;
  } else {
    DWORD dwAppEntrySize = *reinterpret_cast<DWORD *>(base + 8);
    DWORD dwAppArrOffset = *reinterpret_cast<DWORD *>(base + 12);
    DWORD dwAppArrSize = *reinterpret_cast<DWORD *>(base + 16);

    for (DWORD i = 0; i < dwAppArrSize; ++i) {
      uintptr_t entryBaseAddr = reinterpret_cast<uintptr_t>(base) +
                                dwAppArrOffset + (i * dwAppEntrySize);

      char *appNamePtr = reinterpret_cast<char *>(entryBaseAddr + 4);
      std::string applicationName(appNamePtr);

      if (applicationName.find(targetProcess) != std::string::npos) {
        processEntryAddress = entryBaseAddr;

        DWORD rawFrametime = *reinterpret_cast<DWORD *>(processEntryAddress +
                                                        frametimeMemoryOffset);
        return static_cast<double>(rawFrametime) / 1000.0;
      }
    }
  }
  return std::nullopt;
}

} // namespace RTSSReader

namespace InputHandler {

std::optional<WORD> findKey(const std::string &keyToFind) {
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
  INPUT input = {0};
  input.type = INPUT_KEYBOARD;

  input.ki.wVk = vkCode;
  input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
  input.ki.time = 0;
  input.ki.dwExtraInfo = 0;
  input.ki.dwFlags = KEYEVENTF_SCANCODE;

  switch (vkCode) {
  case VK_UP:
  case VK_DOWN:
  case VK_LEFT:
  case VK_RIGHT:
  case VK_HOME:
  case VK_END:
  case VK_PRIOR: // Page Up
  case VK_NEXT:  // Page Down
  case VK_INSERT:
  case VK_DELETE:
  case VK_LCONTROL: // Use VK_LCONTROL and VK_RCONTROL for specific Ctrl keys
  case VK_RCONTROL:
  case VK_LSHIFT: // Using specific shift keys can sometimes be necessary
  case VK_RSHIFT:
  case VK_LMENU: // Left Alt
  case VK_RMENU: // Right Alt (AltGr)
  case VK_APPS:  // The 'context menu' key
    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    break;
  }

  if (!pressDown) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }

  SendInput(1, &input, sizeof(INPUT));
}

struct Task {
  int delay;
  std::optional<std::function<void()>> function;
  bool recursive;
};

std::queue<Task> queuedTasks = {};
std::mutex queuedTasksMutex;

void queueTask(int delay, std::optional<std::function<void()>> function,
               bool recursive) {
  std::lock_guard<std::mutex> lock(queuedTasksMutex);
  queuedTasks.push({delay, function, recursive});
}

void queueInput(WORD vkCode, std::optional<bool> state, bool recursive) {
  auto enqueue = [&](bool press) {
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
        recursive);
  };

  if (state.has_value()) {
    enqueue(state.value());
  } else {
    enqueue(true);
    enqueue(false);
  }
}

std::regex inputPattern(R"((\w+)\s?(down|up|\d+)?(R)?)");

void queueInputs(std::vector<std::string> inputs) {
  for (size_t i = 0; i < inputs.size(); ++i) {
    const std::string &input = inputs[i];
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
      } else if (secondArg == "up") {
        state = false;
      } else if (!secondArg.empty() &&
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
    } else {
      std::optional<WORD> keyOpt = findKey(inputName);
      if (!keyOpt.has_value()) {
        return;
      }
      vkCode = keyOpt.value();

      printf("Key code for '%s': %hd\n", input.c_str(), vkCode);
    }

    for (int i = 0; i < amount; i++) {
      queueInput(vkCode, state, isRecursive);
    }
  }
}

void executeFirstQueuedTask() {
  while (true) {
    Task firstTaskCopy;
    {
      std::lock_guard<std::mutex> lock(queuedTasksMutex);
      if (queuedTasks.empty()) {
        break;
      }
      Task &firstTaskReference = queuedTasks.front();

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
} // namespace InputHandler

class Keybind {
public:
  static std::vector<Keybind> keybinds;

  Keybind(int keyCode, std::function<void()> function) {
    this->keyCode = keyCode;
    this->function = [function]() {
      if (InputHandler::queuedTasks.empty()) {
        InputHandler::queueTask(0, function, false);
      }
    };
    keybinds.push_back(*this);
  }

  Keybind(std::string key, std::function<void()> function) {
    std::optional<WORD> vkCode = InputHandler::findKey(key);
    if (!vkCode.has_value()) {
      fprintf(stderr, "Can't resolve KeyCode from \"%s\"", key.c_str());
      exit(1);
    }

    this->keyCode = vkCode.value();
    this->function = [function]() {
      if (InputHandler::queuedTasks.empty()) {
        InputHandler::queueTask(0, function, false);
      }
    };
    keybinds.push_back(*this);
  }

  DWORD keyCode;
  std::function<void()> function;
};

std::vector<Keybind> Keybind::keybinds = {};

void addKeybinds() {      // Add keybinds here
  new Keybind(220, []() { // You can't type this keycode as a string so i just
                          // typed in the virtual keycode of it instead.
    InputHandler::queueInputs({"m", "enter", "up 3", "enter", "down", "enter"});
  }); // 1 down and then immediately 1 up because it is marked as recursive.
  // This won't actually work because it needs to poll the input as down for
  // at least 1 frame but its still a thing you can do in some situations.

  new Keybind("F2", []() {
    InputHandler::queueInputs(
        {"m", "down 4", "enter", "enter 2", "up", "enter", "m"});
  });
}

HHOOK keyboardHook;

LRESULT CALLBACK onKeyPress(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    KBDLLHOOKSTRUCT *pKeyBoard = (KBDLLHOOKSTRUCT *)lParam;

    // Check if the key event was injected (sent by SendInput() or something
    // idfk how this works bro)
    if (pKeyBoard->flags & LLKHF_INJECTED ||
        getActiveProcessName() != RTSSReader::targetProcess) {
      return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
    }
    switch (wParam) {

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      // lParam is a pointer to a KBDLLHOOKSTRUCT
      KBDLLHOOKSTRUCT *pKeyBoard = (KBDLLHOOKSTRUCT *)lParam;
      DWORD vkCode = pKeyBoard->vkCode;

      for (Keybind &keybind : Keybind::keybinds) {
        if (vkCode == keybind.keyCode) {
          keybind.function();
          return 1;
        }
      }
      // printf("Key Down: %lu\n", vkCode);
      break;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
      KBDLLHOOKSTRUCT *pKeyBoardUp = (KBDLLHOOKSTRUCT *)lParam;
      DWORD vkCode = pKeyBoardUp->vkCode;
      for (Keybind &keybind : Keybind::keybinds) {
        if (vkCode == keybind.keyCode) {
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

int main() {
  if (!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
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
    timeBeginPeriod(1);
    while (true) {
      double frametime = RTSSReader::getRawFrametime().value_or(0);
      if (frametime !=
          previousFrametime) { // This doesn't work if you set an FPS cap using
                               // RTSS but I couldn't find another way to do it
                               // so fuck it, this literally relies on frametime
                               // variance it's really funny
        previousFrametime = frametime;
        InputHandler::executeFirstQueuedTask();
      }

      // If there are currently queued tasks I want to check as often as
      // possible to check for FrameTime updates. This is incredibly bad for the
      // CPU but we should be doing it for very short time periods so it should
      // be OK.
      if (InputHandler::queuedTasks.empty()) {
        Sleep(1);
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