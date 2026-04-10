#include "Keybind.h"
#include <functional>
#include <windows.h>
#include "InputHandler.h"

std::vector<Keybind> Keybind::keybinds;
double Keybind::keybindStartTime = 0.0;

Keybind::Keybind(int keyCode, std::function<void()> exec,
	std::vector<std::string> modifiers) {
	this->keyCode = keyCode;
	this->isPressed = false;
	this->modifiers = modifiers;
	this->function = [exec]() {
		double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
		keybindStartTime = now;
		exec();
		};
	keybinds.push_back(*this);
}

Keybind::Keybind(const std::string& key, std::function<void()> function,
	std::vector<std::string> modifiers)
	: Keybind(InputHandler::getInstance().findKey(key).value(), function,
		modifiers) { // This should always have a value
}