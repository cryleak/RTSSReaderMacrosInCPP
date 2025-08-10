#include "keymap.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mmsystem.h>
#include <mutex>
#include <profileapi.h>
#include <queue>
#include <regex>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>
#include <winuser.h>

#pragma comment(lib, "winmm.lib")
using namespace std::chrono_literals;

namespace RTSSReader {
DWORD frametimeMemoryOffset = 280;
HANDLE hMapFile;
LPVOID pMapAddr;
uintptr_t processEntryAddress;
std::string targetProcess;

void initialize() {
  targetProcess = "GTA5.exe";
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

std::optional<DWORD> findKey(const std::string &keyToFind) {
  std::string lowerCaseKey = keyToFind;
  std::transform(lowerCaseKey.begin(), lowerCaseKey.end(), lowerCaseKey.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  for (size_t i = 0; i < g_key_to_vk_size; ++i) {
    if (g_key_to_vk[i].keyName == lowerCaseKey) {
      return g_key_to_vk[i].vkCode;
    }
  }
  return std::nullopt;
}

void sendKeyInput(WORD vkCode, bool pressDown) {
  INPUT input = {0};
  input.type = INPUT_KEYBOARD;

  input.ki.wVk = vkCode;
  input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
  input.ki.time = 0;

  if (!pressDown) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }

  SendInput(1, &input, sizeof(INPUT));
}

struct Task {
  int delay;
  std::function<void()> function;
  bool recursive;
};

std::queue<Task> queuedTasks = {};
std::mutex queuedTasksMutex;

void queueTask(int delay, std::function<void()> function, bool recursive) {
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
  for (const std::string &input : inputs) {
    std::smatch matches;
    if (!std::regex_match(input, matches, inputPattern)) {
      return;
    }
    std::string inputName = matches[1];
    std::string secondArg = matches[2];
    bool isRecursive = matches[3].matched;

    auto keyOpt = findKey(inputName);

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
    if (inputName == "sleep") {
      for (int i = 0; i < amount; i++) {
        queueInput(VK_F24, false, isRecursive);
      }
      return;
    }

    WORD vkCode;
    if (keyOpt.has_value()) {
      printf("Key code for '%s': %lu\n", input.c_str(), keyOpt.value());
      vkCode = keyOpt.value();
    } else {
      SHORT vk = VkKeyScan(inputName[0]);
      if (vk == -1) {
        printf("Failed to find keycode for: %s", input.c_str());
        continue;
      }
      vkCode = LOBYTE(vk);
    }
    printf("Key code for '%s': %hd\n", input.c_str(), vkCode);
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
    firstTaskCopy.function();
    if (!firstTaskCopy.recursive) {
      break;
    }
  }
}
} // namespace InputHandler

class Keybind {
public:
  static std::vector<Keybind> keybinds;

  Keybind(DWORD keyCode, std::function<void()> function) {
    this->keyCode = keyCode;
    this->function = [function]() {
      InputHandler::queueTask(0, function, false);
    };
    keybinds.push_back(*this);
  }

  DWORD keyCode;
  std::function<void()> function;
};

std::vector<Keybind> Keybind::keybinds = {};

void addKeybinds() { // Add keybinds here
  new Keybind('4', []() {
    InputHandler::queueInputs({"1 downR", "1 up"});
  }); // 1 down and then immediately 1 up because it is marked as recursive.
      // This won't actually work because it needs to poll the input as down for
      // at least 1 frame but its still a thing you can do in some situations.
}

HHOOK keyboardHook;

LRESULT CALLBACK onKeyPress(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
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
    timeBeginPeriod(1);
    while (true) {
      double currentFrametime = RTSSReader::getRawFrametime().value_or(0);
      if (currentFrametime == previousFrametime) {
        previousFrametime = currentFrametime;
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
    timeEndPeriod(1);
  }).detach();

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnhookWindowsHookEx(keyboardHook);
  return 0;
}