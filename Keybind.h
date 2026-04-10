#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

class Keybind {
public:
	Keybind(int keyCode, std::function<void()> exec, std::vector<std::string> modifiers = {});

	Keybind(const std::string& key, std::function<void()> function, std::vector<std::string> modifiers = {});

	static std::vector<Keybind> keybinds;
	static double keybindStartTime;
	bool isPressed;
	DWORD keyCode;
	std::function<void()> function;
	std::vector<std::string> modifiers;
};