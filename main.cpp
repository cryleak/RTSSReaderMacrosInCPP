#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define DEBUG_PRESENT_TIME 0
#include "Gui.h"
#include "HookHandler.h"
#include "InputHandler.h"
#include "Keybind.h"
#include "RTSSReader.h"
#include "Settings.h"
#include "Updater.h"
#include "Utils.h"
#include "keymap.h"

#define NOMINMAX
#include <Windows.h>
#if DEBUG_PRESENT_TIME
#include "PMDPSharedMemory.h"
#endif
#include <intrin.h>
#include <mmsystem.h>
#include <algorithm>
#include <mutex>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "winmm.lib")

using namespace std::chrono_literals;

namespace
{
    class TeeBuffer final : public std::streambuf
    {
    public:
        void set(std::streambuf* first, std::streambuf* second)
        {
            this->first = first;
            this->second = second;
        }

    protected:
        int_type overflow(int_type value) override
        {
            if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
            if (first->sputc(value) == traits_type::eof() || second->sputc(value) == traits_type::eof())
                return traits_type::eof();
            return value;
        }

        int sync() override
        {
            return first->pubsync() == 0 && second->pubsync() == 0 ? 0 : -1;
        }

    private:
        std::streambuf* first = nullptr;
        std::streambuf* second = nullptr;
    };

    class CoutLog
    {
    public:
        bool open()
        {
            file.open("RTSSReaderMacros.log", std::ios::out | std::ios::trunc);
            if (!file) return false;
            original = std::cout.rdbuf();
            tee.set(original, file.rdbuf());
            std::cout.rdbuf(&tee);
            return true;
        }

        ~CoutLog()
        {
            if (!original) return;
            std::cout.flush();
            std::cout.rdbuf(original);
        }

    private:
        std::ofstream file;
        TeeBuffer tee;
        std::streambuf* original = nullptr;
    };

#if DEBUG_PRESENT_TIME
    constexpr DWORD kPresentMonSignature = (static_cast<DWORD>('P') << 24) |
        (static_cast<DWORD>('M') << 16) |
        (static_cast<DWORD>('D') << 8) |
        static_cast<DWORD>('P');

    struct PresentMonSnapshot
    {
        bool available = false;
        bool positionChanged = false;
        bool counterReset = false;
        DWORD status = 0;
        DWORD frameCount = 0;
        DWORD framePos = 0;
        DWORD latestProcessId = 0;
        DWORD latestPresentMode = 0;
        double latestDisplayedTime = 0.0;
        uint64_t framesSincePoll = 0;
    };

    class PresentMonDebug
    {
    public:
        ~PresentMonDebug()
        {
            close();
        }

        PresentMonSnapshot poll()
        {
            PresentMonSnapshot result;
            if (!pMapAddr && !open()) return result;
            if (!pMapAddr || pMapAddr->dwSignature != kPresentMonSignature)
            {
                close();
                return result;
            }

            const DWORD maxFrameCount = static_cast<DWORD>(std::size(pMapAddr->arrFrame));
            if (pMapAddr->dwFrameArrOffset != offsetof(PMDP_SHARED_MEMORY, arrFrame) ||
                pMapAddr->dwFrameArrEntrySize != sizeof(PMDP_FRAME_DATA) ||
                pMapAddr->dwFrameArrSize == 0 || pMapAddr->dwFrameArrSize > maxFrameCount ||
                pMapAddr->dwFramePos >= pMapAddr->dwFrameArrSize)
            {
                close();
                return result;
            }

            result.available = true;
            result.status = pMapAddr->dwStatus;
            result.frameCount = pMapAddr->dwFrameCount;
            result.framePos = pMapAddr->dwFramePos;

            if (hasPreviousFrameCount)
            {
                result.counterReset = result.frameCount < previousFrameCount;
                result.framesSincePoll = result.counterReset ? result.frameCount :
                    result.frameCount - previousFrameCount;
                result.positionChanged = result.framePos != previousFramePos;
            }
            hasPreviousFrameCount = true;
            previousFrameCount = result.frameCount;
            previousFramePos = result.framePos;

            if (result.frameCount)
            {
                const DWORD latestPosition = (result.framePos + pMapAddr->dwFrameArrSize - 1) % pMapAddr->dwFrameArrSize;
                const PMDP_FRAME_DATA& latest = pMapAddr->arrFrame[latestPosition];
                result.latestProcessId = latest.data1.ProcessID;
                result.latestPresentMode = latest.data1.PresentMode;
                result.latestDisplayedTime = latest.data2.DisplayedTime;
            }
            return result;
        }

    private:
        bool open()
        {
            hMapFile = OpenFileMappingW(FILE_MAP_READ, FALSE, L"PMDPSharedMemory");
            if (!hMapFile) return false;
            pMapAddr = reinterpret_cast<PMDP_SHARED_MEMORY*>(MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0));
            if (pMapAddr) return true;
            CloseHandle(hMapFile);
            hMapFile = nullptr;
            return false;
        }

        void close()
        {
            if (pMapAddr) UnmapViewOfFile(pMapAddr);
            if (hMapFile) CloseHandle(hMapFile);
            pMapAddr = nullptr;
            hMapFile = nullptr;
            hasPreviousFrameCount = false;
            previousFrameCount = 0;
            previousFramePos = 0;
        }

        HANDLE hMapFile = nullptr;
        PMDP_SHARED_MEMORY* pMapAddr = nullptr;
        bool hasPreviousFrameCount = false;
        DWORD previousFrameCount = 0;
        DWORD previousFramePos = 0;
    };
#endif

    void enableConsole()
    {
        if (!GetConsoleWindow() && !AllocConsole()) return;

        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
        freopen_s(&stream, "CONIN$", "r", stdin);
        std::cout.clear();
        std::cerr.clear();
        std::cin.clear();
        std::cout << "RTSS Reader Macros console enabled." << std::endl;
    }

    std::string inputName(const KeyChord& key)
    {
        return keyName(key.key);
    }

    struct QuickTurnState
    {
        double driftX = 0.0;
        double driftY = 0.0;
        double lastTurnTime = 0.0;
    };

    std::mutex quickTurnMutex;
    QuickTurnState quickTurnState;
    std::atomic_int enhancedFrameGenerationMultiplier{1};
    std::atomic_bool preciseRtssPolling{false};
    std::atomic_bool frameDetectionCompatibilityMode{false};
    std::atomic_bool maximizeReliability{false};
    std::atomic_int pauseInstructionsInTimespan{1};

    double performanceMilliseconds()
    {
        static const LARGE_INTEGER frequency = []
        {
            LARGE_INTEGER value{};
            QueryPerformanceFrequency(&value);
            return value;
        }();
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
    }

    void quickTurn(const MacroSettings& settings)
    {
        const int screenWidth = std::max(1, GetSystemMetrics(SM_CXSCREEN));
        const double scalar = InputHandler::getInstance().getPhysicalKeyState(VK_RBUTTON) ? 321.435 / 180.0 : 263.0 / 180.0;
        const double pixelsPerDegree = scalar / (3840.0 / static_cast<double>(screenWidth));
        const double now = performanceMilliseconds();
        std::lock_guard lock(quickTurnMutex);
        if (quickTurnState.lastTurnTime != 0.0 && now - quickTurnState.lastTurnTime > 500.0)
        {
            quickTurnState.driftX = 0.0;
            quickTurnState.driftY = 0.0;
        }
        quickTurnState.lastTurnTime = now;

        const double totalPixelsX = -static_cast<double>(settings.quickTurnDegrees) * pixelsPerDegree + quickTurnState.driftX;
        const int moveX = static_cast<int>(std::lround(totalPixelsX));
        quickTurnState.driftX = totalPixelsX - moveX;

        quickTurnState.driftY += 0.032 / (3840.0 / static_cast<double>(screenWidth));
        int moveY = 0;
        if (quickTurnState.driftY >= 1.0)
        {
            moveY = -1;
            quickTurnState.driftY -= 1.0;
        }
        InputHandler::getInstance().sendAutoHotkeyMouseMove(moveX, moveY);
    }

    bool prepareForInteractionMenu()
    {
        InputHandler::getInstance().queueInputs({"lbutton upR", "rbutton upR"});
        return InputHandler::getInstance().getPhysicalKeyState(
            InputHandler::getInstance().findKey("lbutton").value());
    }

    struct AutomaticSwitchState
    {
        bool enabled = false;
        bool horizontal = false;
    };

    std::mutex tabSwitchStateMutex;
    KeyChord lastTabSwitchWeapon;
    std::chrono::steady_clock::time_point lastTabSwitchTime{};

    void resetTabSwitchState()
    {
        std::lock_guard lock(tabSwitchStateMutex);
        lastTabSwitchWeapon = {};
        lastTabSwitchTime = {};
        HookHandler::getInstance().setMovementKeysBlocked(false);
    }

    bool shouldAutomaticallyHandleSwitch(const MacroSettings& settings, const KeyChord& weaponKey)
    {
        if (!settings.automaticLeftClickHandling || weaponKey.empty() || weaponKey == settings.stickyBombKey ||
            settings.sprintKey.empty() ||
            !InputHandler::getInstance().getPhysicalKeyState(settings.sprintKey.key))
            return false;

        std::lock_guard lock(tabSwitchStateMutex);
        return !(lastTabSwitchWeapon == settings.stickyBombKey) ||
            std::chrono::steady_clock::now() - lastTabSwitchTime > 390ms;
    }

    AutomaticSwitchState prepareAutomaticSwitch(const MacroSettings& settings, const KeyChord& weaponKey)
    {
        AutomaticSwitchState state;
        state.enabled = shouldAutomaticallyHandleSwitch(settings, weaponKey);
        state.horizontal = state.enabled && settings.automaticHorizontalKeyHandling;
        if (!state.enabled) return state;

        if (state.horizontal)
        {
            HookHandler::getInstance().setMovementKeysBlocked(true);
            InputHandler::getInstance().queueInputs({"a upR", "d upR"});
        }
        InputHandler::getInstance().queueInputs({"lbutton upR"});
        return state;
    }

    void queueAutomaticSwitchRestore(const AutomaticSwitchState& state)
    {
        if (!state.enabled) return;
        InputHandler::getInstance().queueTaskAfter(100ms, [horizontal = state.horizontal]
        {
            auto& input = InputHandler::getInstance();
            if (input.getPhysicalKeyState(VK_LBUTTON)) input.sendKeyInput(VK_LBUTTON, true);
            if (horizontal)
            {
                if (input.getPhysicalKeyState('A')) input.sendKeyInput('A', true);
                if (input.getPhysicalKeyState('D')) input.sendKeyInput('D', true);
                HookHandler::getInstance().setMovementKeysBlocked(false);
            }
        });

        InputHandler::getInstance().queueTaskAfter(105ms, [horizontal = state.horizontal]
        {
            auto& input = InputHandler::getInstance();
            if (horizontal)
            {
                if (input.getPhysicalKeyState('A')) input.sendKeyInput('A', true);
                if (input.getPhysicalKeyState('D')) input.sendKeyInput('D', true);
                HookHandler::getInstance().setMovementKeysBlocked(false);
            }
        });
    }

    void rememberTabSwitch(const KeyChord& weaponKey)
    {
        std::lock_guard lock(tabSwitchStateMutex);
        lastTabSwitchWeapon = weaponKey;
        lastTabSwitchTime = std::chrono::steady_clock::now();
    }

    void ensureInteractionMenuOpen(const std::string& interactionMenuKey)
    {
        InputHandler::getInstance().queueInputs({interactionMenuKey + " down", "sleep"});
    }

    void ensureInteractionMenuClose(const std::string& interactionMenuKey)
    {
        InputHandler::getInstance().queueInputs({interactionMenuKey + " down", "sleep", interactionMenuKey + " upR"});
    }

    void queueBst(const MacroSettings& settings)
    {
        prepareForInteractionMenu();
        InputHandler::getInstance().queueInputs({
            "enter downR", inputName(settings.interactionMenuKey), "enter up", "enter downR",
            "up 3", "enter up", "enter downR", "down down", "enter upR", "down upR"
        });
    }

    void queueThermal(const MacroSettings& settings)
    {
        prepareForInteractionMenu();
        const std::string interactionMenuKey = inputName(settings.interactionMenuKey);
        InputHandler::getInstance().queueInputs({
            "enter downR", interactionMenuKey, "down 5", "enter up", "down downR",
            "enter down", "down up", "enter upR",
        });
        if (!settings.thermalNightVision)
        {
            InputHandler::getInstance().queueInputs({"sleep", "down 4"});
        }
        InputHandler::getInstance().queueInputs({
            "sleep 2", "space downR",
            interactionMenuKey + "R", "space upR"
        });
    }

    void queueSnacks(const MacroSettings& settings)
    {
        prepareForInteractionMenu();
        InputHandler::getInstance().queueInputs({
            inputName(settings.interactionMenuKey) + "R", "enter down", "down 4", "enter up",
            "down downR", "enter down", "down up", "down", "enter up"
        });
    }

    void queueAmmo(const MacroSettings& settings)
    {
        const std::string interactionMenuKey = inputName(settings.interactionMenuKey);
        if (settings.useCursorMacros)
        {
            InputHandler::getInstance().lockCursorTo(0.0911458, 0.284259);
            bool leftClickPressed = prepareForInteractionMenu();
            InputHandler::getInstance().queueInputs({"enter downR"});
            ensureInteractionMenuOpen(interactionMenuKey);
            InputHandler::getInstance().queueInputs({
                "sleep", "lbutton down", interactionMenuKey + " up", "lbutton up", "enter up",
                "enter 2", "enter downR", "up down", "enter upR", "up upR"
            });
            ensureInteractionMenuClose(interactionMenuKey);
            InputHandler::getInstance().queueTask(0, [] { InputHandler::getInstance().releaseCursor(); }, true);
            if (settings.repressLeftClick && leftClickPressed)
                InputHandler::getInstance().queueInputs({"lbutton downR"});
            return;
        }

        InputHandler::getInstance().queueInputs({
            interactionMenuKey + "R", "enter down", "down 4", "enter up", "enter 2",
            "enter downR", "up down", "enter upR", "up upR"
        });
        ensureInteractionMenuClose(interactionMenuKey);
    }

    void queueWeaponSwitch(const MacroSettings& settings, const KeyChord& weaponKey)
    {
        if (weaponKey.empty() || settings.weaponWheelKey.empty()) return;
        const AutomaticSwitchState automatic = prepareAutomaticSwitch(settings, weaponKey);
        InputHandler::getInstance().queueInputs({
                                                    inputName(weaponKey) + " down", "sleep", inputName(settings.weaponWheelKey) + "R", inputName(weaponKey) + " up"
                                                },
                                                [automatic] { queueAutomaticSwitchRestore(automatic); });
        rememberTabSwitch(weaponKey);
    }

    void queueExplicitWeaponSwitch(const MacroSettings& settings, const KeyChord& weaponKey, int pressAmount)
    {
        if (settings.fistsKey.empty() || weaponKey.empty() || settings.weaponWheelKey.empty()) return;
        auto& input = InputHandler::getInstance();
        const bool leftButtonWasDown = settings.repressLeftClick && input.getPhysicalKeyState(VK_LBUTTON);
        std::vector<std::string> inputs = {
            "lbutton upR", inputName(settings.fistsKey) + " down"
        };
        for (int i = 1; i < pressAmount; ++i) inputs.push_back(inputName(weaponKey));
        inputs.push_back(inputName(weaponKey) + " down");
        inputs.push_back(inputName(settings.weaponWheelKey) + "R");
        inputs.push_back(inputName(settings.fistsKey) + " upR");
        inputs.push_back(inputName(weaponKey) + " upR");
        input.queueInputs(std::move(inputs), [leftButtonWasDown]
        {
            if (leftButtonWasDown) InputHandler::getInstance().sendKeyInput(VK_LBUTTON, true);
        });
    }

    void queueSafeHeavySwap(const MacroSettings& settings)
    {
        if (settings.meleeKey.empty() || settings.rpgKey.empty() || settings.weaponWheelKey.empty()) return;
        InputHandler::getInstance().queueInputs({
            inputName(settings.meleeKey) + " down", inputName(settings.rpgKey) + " down",
            inputName(settings.weaponWheelKey) + "R", inputName(settings.meleeKey) + " upR",
            inputName(settings.rpgKey) + " upR"
        });
    }

    void queueRpgSpam(const MacroSettings& settings)
    {
        InputHandler::getInstance().queueInputs({
            inputName(settings.stickyBombKey) + " down", "sleep 2", inputName(settings.rpgKey) + " down",
            inputName(settings.weaponWheelKey) + "R", inputName(settings.rpgKey) + " upR",
            inputName(settings.stickyBombKey) + " upR"
        });
    }

    void queueSniperSpam(const MacroSettings& settings)
    {
        InputHandler::getInstance().queueInputs({
            inputName(settings.stickyBombKey) + " down", inputName(settings.sniperKey) + " down",
            inputName(settings.weaponWheelKey) + "R", inputName(settings.sniperKey) + " upR",
            inputName(settings.stickyBombKey) + " upR"
        });
    }

    void queueDoubleSwitch(const MacroSettings& settings)
    {
        if (settings.rpgKey.empty() || settings.weaponWheelKey.empty()) return;
        const AutomaticSwitchState automatic = prepareAutomaticSwitch(settings, settings.rpgKey);
        InputHandler::getInstance().queueInputs({
                                                    inputName(settings.rpgKey), inputName(settings.rpgKey) + " down",
                                                    inputName(settings.weaponWheelKey) + "R", inputName(settings.rpgKey) + " upR"
                                                },
                                                [automatic] { queueAutomaticSwitchRestore(automatic); });
        rememberTabSwitch(settings.rpgKey);
    }

    void queueTripleSwitch(const MacroSettings& settings)
    {
        if (settings.rpgKey.empty() || settings.weaponWheelKey.empty()) return;
        const AutomaticSwitchState automatic = prepareAutomaticSwitch(settings, settings.rpgKey);
        InputHandler::getInstance().queueInputs({
                                                    inputName(settings.rpgKey) + " 2", inputName(settings.rpgKey) + " down",
                                                    inputName(settings.weaponWheelKey) + "R", inputName(settings.rpgKey) + " upR",
                                                }, [automatic] { queueAutomaticSwitchRestore(automatic); });
        rememberTabSwitch(settings.rpgKey);
    }

    bool validateSettings(const MacroSettings& settings, std::string& error)
    {
        if (settings.frameGenerationMultiplier < 1 || settings.frameGenerationMultiplier > 4)
        {
            error = "Enhanced frame generation multiplier must be between 1 and 4.";
            return false;
        }
        const std::array<std::pair<const KeyChord*, const char*>, 24> triggers = {
            {
                {&settings.bstHotkey, "BST"}, {&settings.thermalHotkey, "thermal"}, {&settings.snacksHotkey, "snacks"},
                {&settings.ammoHotkey, "ammo"}, {&settings.quickTurnHotkey, "quick turn"}, {&settings.rpgTabSwitchHotkey, "RPG tab switch"},
                {&settings.stickyBombTabSwitchHotkey, "sticky bomb tab switch"},
                {&settings.sniperTabSwitchHotkey, "sniper tab switch"},
                {&settings.pistolTabSwitchHotkey, "pistol tab switch"},
                {&settings.shotgunTabSwitchHotkey, "shotgun tab switch"},
                {&settings.rifleTabSwitchHotkey, "rifle tab switch"},
                {&settings.smgTabSwitchHotkey, "SMG tab switch"},
                {&settings.fistsTabSwitchHotkey, "fists tab switch"},
                {&settings.meleeTabSwitchHotkey, "melee tab switch"},
                {&settings.rpgSpamHotkey, "RPG spam"}, {&settings.sniperSpamHotkey, "sniper spam"}, {&settings.doubleSwitchHotkey, "double switch"}, {&settings.tripleSwitchHotkey, "triple switch"},
                {&settings.chatKey, "chat"}, {&settings.explicitRpgSwitchHotkey, "explicit RPG switch"},
                {&settings.explicitHomingSwitchHotkey, "explicit homing switch"},
                {&settings.explicitGrenadeSwitchHotkey, "explicit grenade switch"},
                {&settings.safeHeavySwapHotkey, "safe heavy swap"},
                {&settings.suspendHotkey, "suspend macros"},
            }
        };
        for (size_t i = 0; i < triggers.size(); ++i)
        {
            if (triggers[i].first->empty()) continue;
            for (size_t j = i + 1; j < triggers.size(); ++j)
            {
                if (!triggers[j].first->empty() && *triggers[i].first == *triggers[j].first)
                {
                    error = std::string("Macro hotkey is used twice: ") + triggers[i].second + " and " + triggers[j].second;
                    return false;
                }
            }
        }
        return true;
    }

    void installKeybinds(const MacroSettings& settings)
    {
        Keybind::clear();
        Keybind::add(settings.bstHotkey, [settings] { queueBst(settings); }, false, "BST");
        Keybind::add(settings.thermalHotkey, [settings] { queueThermal(settings); }, false, "Thermal");
        Keybind::add(settings.snacksHotkey, [settings] { queueSnacks(settings); }, false, "Snacks");
        Keybind::add(settings.ammoHotkey, [settings] { queueAmmo(settings); }, false, "Ammo");
        Keybind::add(settings.quickTurnHotkey, [settings] { quickTurn(settings); }, false, "Quick turn");

        Keybind::add(settings.rpgTabSwitchHotkey, [settings] { queueWeaponSwitch(settings, settings.rpgKey); }, false, "RPG tab switch");
        Keybind::add(settings.stickyBombTabSwitchHotkey, [settings] { queueWeaponSwitch(settings, settings.stickyBombKey); }, false, "Sticky bomb tab switch");
        Keybind::add(settings.sniperTabSwitchHotkey, [settings] { queueWeaponSwitch(settings, settings.sniperKey); }, false, "Sniper tab switch");
        Keybind::add(settings.pistolTabSwitchHotkey, [settings] { queueWeaponSwitch(settings, settings.pistolKey); }, false, "Pistol tab switch");
        Keybind::add(settings.shotgunTabSwitchHotkey, [settings] { queueWeaponSwitch(settings, settings.shotgunKey); }, false, "Shotgun tab switch");
        Keybind::add(settings.rifleTabSwitchHotkey, [settings] { queueWeaponSwitch(settings, settings.rifleKey); }, false, "Rifle tab switch");
        Keybind::add(settings.smgTabSwitchHotkey, [settings] { queueWeaponSwitch(settings, settings.smgKey); }, false, "SMG tab switch");
        Keybind::add(settings.fistsTabSwitchHotkey, [settings] { queueWeaponSwitch(settings, settings.fistsKey); }, false, "Fists tab switch");
        Keybind::add(settings.meleeTabSwitchHotkey, [settings] { queueWeaponSwitch(settings, settings.meleeKey); }, false, "Melee tab switch");

        Keybind::add(settings.rpgSpamHotkey, [settings] { queueRpgSpam(settings); }, false, "RPG spam");
        Keybind::add(settings.sniperSpamHotkey, [settings] { queueSniperSpam(settings); }, false, "Sniper spam");
        Keybind::add(settings.doubleSwitchHotkey, [settings] { queueDoubleSwitch(settings); }, false, "Double switch");
        Keybind::add(settings.tripleSwitchHotkey, [settings] { queueTripleSwitch(settings); }, false, "Triple switch");
        Keybind::add(settings.explicitRpgSwitchHotkey, [settings] { queueExplicitWeaponSwitch(settings, settings.rpgKey, 1); }, false, "Explicit RPG switch");
        Keybind::add(settings.explicitHomingSwitchHotkey, [settings] { queueExplicitWeaponSwitch(settings, settings.rpgKey, 2); }, false, "Explicit homing switch");
        Keybind::add(settings.explicitGrenadeSwitchHotkey, [settings] { queueExplicitWeaponSwitch(settings, settings.rpgKey, 3); }, false, "Explicit grenade switch");
        Keybind::add(settings.safeHeavySwapHotkey, [settings] { queueSafeHeavySwap(settings); }, false, "Safe heavy swap");
        Keybind::add(settings.suspendHotkey, []
        {
        }, true, "Suspend", true);
        Keybind::add(settings.chatKey, [settings]
        {
            HookHandler::getInstance().inChat = true;
            InputHandler::getInstance().queueInputs({inputName(settings.chatKey)});
        }, true, "Chat");
    }

    bool applySettings(const MacroSettings& settings, std::string& error)
    {
        if (!validateSettings(settings, error)) return false;
        if (!SettingsStore::save(settings, error)) return false;
        enhancedFrameGenerationMultiplier.store(std::clamp(settings.frameGenerationMultiplier, 1, 4), std::memory_order_relaxed);
        preciseRtssPolling.store(settings.preciseRtssPolling, std::memory_order_relaxed);
        frameDetectionCompatibilityMode.store(settings.frameDetectionCompatibilityMode, std::memory_order_relaxed);
        maximizeReliability.store(settings.maximizeReliability, std::memory_order_relaxed);
        InputHandler::getInstance().clearQueuedTasks();
        InputHandler::getInstance().releaseCursor();
        HookHandler::getInstance().inChat = false;
        resetTabSwitchState();
        installKeybinds(settings);
        return true;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int)
{
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) SetProcessDPIAware();
#if DEBUG_PRESENT_TIME
    constexpr bool outputEnabled = true;
#else
    const bool outputEnabled = commandLine && wcsstr(commandLine, L"--console");
#endif
    CoutLog coutLog;
    if (outputEnabled) coutLog.open();
    if (outputEnabled) enableConsole();
    if (commandLine && wcsstr(commandLine, L"--self-test"))
    {
        std::string error;
        return runSettingsSelfTest(error) && Updater::selfTest(error) ? 0 : 1;
    }

    std::vector<std::string> warnings;
    MacroSettings settings = SettingsStore::load(&warnings);
    enhancedFrameGenerationMultiplier.store(std::clamp(settings.frameGenerationMultiplier, 1, 4), std::memory_order_relaxed);
    preciseRtssPolling.store(settings.preciseRtssPolling, std::memory_order_relaxed);
    frameDetectionCompatibilityMode.store(settings.frameDetectionCompatibilityMode, std::memory_order_relaxed);
    maximizeReliability.store(settings.maximizeReliability, std::memory_order_relaxed);
    installKeybinds(settings);

    NativeGui& gui = NativeGui::getInstance();
    std::thread updateCheckThread;
    std::thread updateInstallThread;
    std::atomic_bool updateInstallRunning = false;
    auto startUpdateInstall = [&](const Updater::UpdateInfo& info)
    {
        if (updateInstallRunning.exchange(true)) return;
        if (updateInstallThread.joinable()) updateInstallThread.join();
        updateInstallThread = std::thread([&gui, info, &updateInstallRunning]
        {
            const Updater::InstallResult result = Updater::downloadAndInstall(info);
            updateInstallRunning.store(false);
            gui.postUpdateInstallResult(result);
        });
    };
    if (!gui.create(instance, settings, applySettings, startUpdateInstall))
    {
        MessageBoxW(nullptr, L"Could not create the RTSS Reader Macros window.", L"RTSS Reader Macros", MB_OK | MB_ICONERROR);
        return 1;
    }

    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    if (!HookHandler::getInstance().addKeyboardHook() || !HookHandler::getInstance().addMouseHook())
    {
        MessageBoxW(gui.window(), L"Could not install the global input hooks.", L"RTSS Reader Macros", MB_OK | MB_ICONERROR);
        gui.exit();
        return 1;
    }
    updateCheckThread = std::thread([&gui]
    {
        gui.postUpdateCheck(Updater::checkForUpdate());
    });

    const auto pauseStart = startTiming();
    uint64_t pauseCount = 0;
    while (std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - pauseStart).count() < 100.0)
    {
        _mm_pause();
        ++pauseCount;
    }
    const double pauseTestMilliseconds = stopTiming(pauseStart);
    const double pausesInThreshold = // 15 microseconds
        static_cast<double>(pauseCount) * 15.0 / (pauseTestMilliseconds * 1000.0);
    pauseInstructionsInTimespan.store(std::max(1, static_cast<int>(std::llround(pausesInThreshold))), std::memory_order_relaxed);
    std::cout << "_mm_pause count in " << pauseTestMilliseconds << "ms: " << pauseCount
        << "; estimated pauses in timespan: " << pausesInThreshold << std::endl;

    std::atomic_bool running = true;
    std::thread rtssThread([&]
    {
        RTSSReader& reader = RTSSReader::getInstance();
        RtssStatus status = reader.status();
        uint64_t generation = status.generation;
        auto nextRefresh = std::chrono::steady_clock::now();
        while (running)
        {
            auto now = std::chrono::steady_clock::now();
            if (now >= nextRefresh)
            {
                status = reader.refresh();
                nextRefresh = std::chrono::steady_clock::now() + 250ms;
                if (status.generation != generation)
                {
                    generation = status.generation;
                    HookHandler::getInstance().setTargetProcess(status.targetPid, status.targetProcess);
                    InputHandler::getInstance().clearQueuedTasks();
                    InputHandler::getInstance().releaseCursor();
                    resetTabSwitchState();
                }
                gui.postRtssStatus(status);
            }
            std::this_thread::sleep_for(10ms);
        }
        reader.close();
    });

    std::thread frameThread([&]
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        timeBeginPeriod(1);
        HANDLE highResolutionTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        uint64_t previousPresentTime = 0;
#if DEBUG_PRESENT_TIME
        constexpr std::array<const char*, 17> timingNames = {
            "qwInputSampleTime", "qwSimStartTime", "qwSimEndTime",
            "qwRenderSubmitStartTime", "qwRenderSubmitEndTime",
            "qwPresentStartTime", "qwPresentEndTime",
            "qwDriverStartTime", "qwDriverEndTime",
            "qwOsRenderQueueStartTime", "qwOsRenderQueueEndTime",
            "qwGpuRenderStartTime", "qwGpuRenderEndTime", "dwFrameTime",
            "dwGpuActiveRenderTime", "dwGpuFrameTime", "dwStatFrameTimeBufPos",
        };
        auto previousTimingValues = RTSSReader::getInstance().timingValues();
        auto timingChangeCounts = previousTimingValues;
        timingChangeCounts.fill(0);
        auto nextDebugOutput = std::chrono::steady_clock::now() + 1s;
        PresentMonDebug presentMonDebug;
        PresentMonSnapshot presentMonSnapshot;
        constexpr std::array<const char*, 6> presentMonNames = {
            "dwFrameCount", "dwFramePos", "status", "latestProcessId", "latestPresentMode", "latestDisplayedTime",
        };
        std::array<double, 6> presentMonValues{};
        std::array<double, 6> previousPresentMonValues{};
        std::array<uint64_t, 6> presentMonChangeCounts{};
        bool hasPreviousPresentMonValues = false;
        uint64_t presentMonFramesThisSecond = 0;
        uint64_t presentMonPositionChangesThisSecond = 0;
#endif
        int framesDetected = 0;
        while (running)
        {
            // auto start = startTiming();
            uint64_t present = RTSSReader::getInstance().presentTime(
                frameDetectionCompatibilityMode.load(std::memory_order_relaxed));
#if DEBUG_PRESENT_TIME
            const auto timingValues = RTSSReader::getInstance().timingValues();
            // ponytail: counts changes observed between polls; exact write counts need an RTSS event/counter.
            for (size_t i = 0; i < timingValues.size(); ++i)
            {
                if (timingValues[i] != previousTimingValues[i])
                {
                    ++timingChangeCounts[i];
                    previousTimingValues[i] = timingValues[i];
                }
            }
            const auto presentMon = presentMonDebug.poll();
            presentMonSnapshot = presentMon;
            if (presentMon.available)
            {
                presentMonFramesThisSecond += presentMon.framesSincePoll;
                if (presentMon.positionChanged) ++presentMonPositionChangesThisSecond;
                presentMonValues = {
                    static_cast<double>(presentMon.frameCount),
                    static_cast<double>(presentMon.framePos),
                    static_cast<double>(presentMon.status),
                    static_cast<double>(presentMon.latestProcessId),
                    static_cast<double>(presentMon.latestPresentMode),
                    std::isfinite(presentMon.latestDisplayedTime) ? presentMon.latestDisplayedTime : 0.0,
                };
                if (hasPreviousPresentMonValues)
                {
                    // ponytail: counts PMDP field transitions between polls; dwFrameCount delta remains the frame total.
                    for (size_t i = 0; i < presentMonValues.size(); ++i)
                    {
                        if (presentMonValues[i] != previousPresentMonValues[i])
                            ++presentMonChangeCounts[i];
                    }
                }
                else
                {
                    hasPreviousPresentMonValues = true;
                }
                previousPresentMonValues = presentMonValues;
            }
            else
            {
                hasPreviousPresentMonValues = false;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextDebugOutput)
            {
                size_t changedFields = 0;
                uint64_t totalChanges = 0;
                for (const uint64_t count : timingChangeCounts)
                {
                    if (count) ++changedFields;
                    totalChanges += count;
                }
                std::cout << "RTSS timing values: " << changedFields << "/" << timingValues.size()
                    << " fields changed, " << totalChanges << " total observed changes in 1s" << std::endl;
                for (size_t i = 0; i < timingValues.size(); ++i)
                {
                    std::cout << "  " << timingNames[i] << ": " << timingValues[i]
                        << " (" << timingChangeCounts[i] << " changes)" << std::endl;
                }
                size_t presentMonChangedFields = 0;
                uint64_t presentMonTotalChanges = 0;
                for (const uint64_t count : presentMonChangeCounts)
                {
                    if (count) ++presentMonChangedFields;
                    presentMonTotalChanges += count;
                }
                if (!presentMonSnapshot.available)
                {
                    std::cout << "PresentMon: PMDPSharedMemory unavailable, " << presentMonChangedFields
                        << "/" << presentMonChangeCounts.size() << " fields changed, "
                        << presentMonTotalChanges << " total observed changes in 1s" << std::endl;
                }
                else
                {
                    std::cout << "PresentMon: " << presentMonChangedFields << "/" << presentMonChangeCounts.size()
                        << " fields changed, " << presentMonTotalChanges << " total observed changes in 1s, "
                        << presentMonFramesThisSecond
                        << " new frames in 1s, " << presentMonPositionChangesThisSecond
                        << " observed dwFramePos changes, dwFrameCount=" << presentMonSnapshot.frameCount
                        << ", dwFramePos=" << presentMonSnapshot.framePos
                        << ", status=" << presentMonSnapshot.status
                        << ", latest PID=" << presentMonSnapshot.latestProcessId
                        << ", PresentMode=" << presentMonSnapshot.latestPresentMode
                        << ", DisplayedTime=" << presentMonSnapshot.latestDisplayedTime << " ms";
                    if (presentMonSnapshot.counterReset) std::cout << " (counter reset)";
                    std::cout << std::endl;
                    for (size_t i = 0; i < presentMonValues.size(); ++i)
                    {
                        std::cout << "  PresentMon." << presentMonNames[i] << ": " << presentMonValues[i]
                            << " (" << presentMonChangeCounts[i] << " changes)" << std::endl;
                    }
                }
                timingChangeCounts.fill(0);
                presentMonChangeCounts.fill(0);
                presentMonFramesThisSecond = 0;
                presentMonPositionChangesThisSecond = 0;
                nextDebugOutput = now + 1s;
            }
#endif
            if (present && present != previousPresentTime)
            {
                previousPresentTime = present;
                const bool enhanced = HookHandler::getInstance().isEnhancedTarget();
                const int multiplier = std::max(
                    maximizeReliability.load(std::memory_order_relaxed) ? 2 : 1,
                    enhanced ? std::clamp(enhancedFrameGenerationMultiplier.load(std::memory_order_relaxed), 1, 4) : 1);
                if (++framesDetected >= multiplier)
                {
                    framesDetected = 0;
                    InputHandler::getInstance().executeFirstQueuedTask();
                }
            }

            if (!InputHandler::getInstance().hasQueuedTasks())
            {
#if DEBUG_PRESENT_TIME
                constexpr bool precisePolling = true;
#else
                const bool precisePolling = preciseRtssPolling.load(std::memory_order_relaxed);
#endif
                if (precisePolling)
                {
                    const int pauseCount = pauseInstructionsInTimespan.load(std::memory_order_relaxed);
                    for (int i = 0; i < pauseCount * 1.5; ++i) _mm_pause(); // multiply by 1.5 because idk
                }
                else
                {
                    LARGE_INTEGER dueTime{};
                    dueTime.QuadPart = -5000;
                    SetWaitableTimer(highResolutionTimer, &dueTime, 0, nullptr, nullptr, FALSE);
                    WaitForSingleObject(highResolutionTimer, INFINITE);
                }
            }
            else
            {
                _mm_pause(); // call once cause i think its good to do while spinwaiting in general
            }
            // auto stop = stopTiming(start);
            // std::cout << "Took " << stop << "ms" << std::endl;
        }
        if (highResolutionTimer) CloseHandle(highResolutionTimer);
        timeEndPeriod(1);
    });

    MSG message{};
    while (true)
    {
        int result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) break;
        if (message.message == WM_APP_REHOOK_KEYBOARD)
        {
            HookHandler::getInstance().addKeyboardHook();
            continue;
        }
        if (message.message == WM_APP_REHOOK_MOUSE)
        {
            HookHandler::getInstance().addMouseHook();
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    running = false;
    if (updateCheckThread.joinable()) updateCheckThread.join();
    if (updateInstallThread.joinable()) updateInstallThread.join();
    if (rtssThread.joinable()) rtssThread.join();
    if (frameThread.joinable()) frameThread.join();
    InputHandler::getInstance().releaseCursor();
    HookHandler::getInstance().removeKeyboardHook();
    HookHandler::getInstance().removeMouseHook();
    return 0;
}
