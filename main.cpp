#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
#include <intrin.h>
#include <mmsystem.h>
#include <algorithm>
#include <mutex>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "winmm.lib")

using namespace std::chrono_literals;

namespace
{
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
    if (commandLine && wcsstr(commandLine, L"--console")) enableConsole();
    if (commandLine && wcsstr(commandLine, L"--self-test"))
    {
        std::string error;
        return runSettingsSelfTest(error) && Updater::selfTest(error) ? 0 : 1;
    }

    std::vector<std::string> warnings;
    MacroSettings settings = SettingsStore::load(&warnings);
    enhancedFrameGenerationMultiplier.store(std::clamp(settings.frameGenerationMultiplier, 1, 4), std::memory_order_relaxed);
    preciseRtssPolling.store(settings.preciseRtssPolling, std::memory_order_relaxed);
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
        int framesDetected = 0;
        while (running)
        {
            // auto start = startTiming();
            uint64_t present = RTSSReader::getInstance().presentTime();
            if (present && present != previousPresentTime)
            {
                previousPresentTime = present;
                const bool enhanced = HookHandler::getInstance().isEnhancedTarget();
                if (!enhanced) framesDetected = 0;
                const int multiplier = std::clamp(enhancedFrameGenerationMultiplier.load(std::memory_order_relaxed), 1, 4);
                if (!enhanced || ++framesDetected >= multiplier)
                {
                    framesDetected = 0;
                    InputHandler::getInstance().executeFirstQueuedTask();
                }
            }

            if (!InputHandler::getInstance().hasQueuedTasks())
            {
                if (preciseRtssPolling.load(std::memory_order_relaxed))
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
