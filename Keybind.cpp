#include "Keybind.h"

#include <chrono>

std::vector<Keybind> Keybind::keybinds;
std::mutex Keybind::mutex;
std::atomic_bool Keybind::suspended = false;
double Keybind::keybindStartTime = 0.0;

void Keybind::clear() {
	std::lock_guard lock(mutex);
	keybinds.clear();
	suspended.store(false, std::memory_order_relaxed);
}

void Keybind::add(const KeyChord& chord, std::function<void()> function, bool allowInChat, std::string name, bool suspendToggle) {
	if (chord.empty() || !function) return;
	Keybind keybind;
	keybind.keyCode = chord.key;
	keybind.modifiers = chord.modifiers;
	keybind.function = [function = std::move(function), suspendToggle] {
		keybindStartTime = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		if (suspendToggle) suspended.store(!suspended.load(std::memory_order_relaxed), std::memory_order_relaxed);
		function();
	};
	keybind.allowInChat = allowInChat;
	keybind.name = std::move(name);
	keybind.suspendToggle = suspendToggle;
	std::lock_guard lock(mutex);
	keybinds.push_back(std::move(keybind));
}

bool Keybind::modifiersPressed(const Keybind& keybind) {
	for (WORD modifier : keybind.modifiers) {
		bool pressed = false;
		for (const Keybind& other : keybinds) {
			if (other.keyCode == modifier && other.isPressedState) {
				pressed = true;
				break;
			}
		}
		if (!pressed && !(GetAsyncKeyState(modifier) & 0x8000)) return false;
	}
	return true;
}

bool Keybind::dispatchDown(WORD keyCode, bool inChat, bool legacyGame) {
	std::function<void()> action;
	bool handled = false;
	{
		std::lock_guard lock(mutex);
		for (Keybind& keybind : keybinds) {
			if (keybind.keyCode != keyCode || keybind.isPressedState || !modifiersPressed(keybind) ||
				(suspended.load(std::memory_order_relaxed) && !keybind.suspendToggle) ||
				(!legacyGame && keybind.name == "Chat") || (inChat && !keybind.allowInChat)) continue;
			keybind.isPressedState = true;
			handled = true;
			action = keybind.function;
			break;
		}
	}
	if (action) action();
	return handled;
}

bool Keybind::dispatchUp(WORD keyCode) {
	std::lock_guard lock(mutex);
	bool handled = false;
	for (Keybind& keybind : keybinds) {
		if (keybind.keyCode == keyCode && keybind.isPressedState) {
			keybind.isPressedState = false;
			handled = true;
		}
	}
	return handled;
}

bool Keybind::dispatchKeyDown(WORD keyCode, bool inChat, bool legacyGame) {
	return dispatchDown(keyCode, inChat, legacyGame);
}

bool Keybind::dispatchKeyUp(WORD keyCode) {
	return dispatchUp(keyCode);
}

bool Keybind::dispatchMouseDown(WORD keyCode, bool inChat, bool legacyGame) {
	bool handled = dispatchDown(keyCode, inChat, legacyGame);
	if (keyCode >= 0x1000 && keyCode <= 0x1003) dispatchUp(keyCode);
	return handled;
}

bool Keybind::dispatchMouseUp(WORD keyCode) {
	return dispatchUp(keyCode);
}

bool Keybind::isPressed(WORD keyCode) {
	std::lock_guard lock(mutex);
	for (const Keybind& keybind : keybinds) {
		if (keybind.keyCode == keyCode && keybind.isPressedState) return true;
	}
	return false;
}
