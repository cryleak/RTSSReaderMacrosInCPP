#pragma once

#include "Settings.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class Keybind
{
public:
    static void clear();
    static void add(const KeyChord& chord, std::function<void()> function,
                    bool allowInChat = false, std::string name = {}, bool suspendToggle = false);

    static bool dispatchKeyDown(WORD keyCode, bool inChat, bool legacyGame = true);
    static bool dispatchKeyUp(WORD keyCode);
    static bool dispatchMouseDown(WORD keyCode, bool inChat, bool legacyGame = true);
    static bool dispatchMouseUp(WORD keyCode);
    static bool isPressed(WORD keyCode);

    static double keybindStartTime;

    WORD keyCode = 0;
    std::vector<WORD> modifiers;
    bool isPressedState = false;
    std::function<void()> function;
    bool allowInChat = false;
    std::string name;
    bool suspendToggle = false;

private:
    static std::vector<Keybind> keybinds;
    static std::mutex mutex;
    static std::atomic_bool suspended;
    static bool modifiersPressed(const Keybind& keybind);
    static bool dispatchDown(WORD keyCode, bool inChat, bool legacyGame);
    static bool dispatchUp(WORD keyCode);
};
