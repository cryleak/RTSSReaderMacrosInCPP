#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "Gui.h"
#include "keymap.h"

#include <Windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <stdexcept>
#include <utility>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace {
using Microsoft::WRL::ComPtr;

const D2D1::ColorF kBackground(0.025f, 0.030f, 0.050f, 1.0f);
const D2D1::ColorF kBackgroundTop(0.020f, 0.030f, 0.070f, 1.0f);
const D2D1::ColorF kBackgroundBottom(0.055f, 0.025f, 0.085f, 1.0f);
const D2D1::ColorF kSidebar(0.040f, 0.050f, 0.090f, 1.0f);
const D2D1::ColorF kSidebarTop(0.040f, 0.060f, 0.120f, 1.0f);
const D2D1::ColorF kSidebarBottom(0.028f, 0.032f, 0.065f, 1.0f);
const D2D1::ColorF kCard(0.073f, 0.086f, 0.116f, 1.0f);
const D2D1::ColorF kCardTop(0.095f, 0.115f, 0.175f, 1.0f);
const D2D1::ColorF kCardBottom(0.070f, 0.075f, 0.130f, 1.0f);
const D2D1::ColorF kCardHover(0.095f, 0.112f, 0.150f, 1.0f);
const D2D1::ColorF kCardHoverTop(0.125f, 0.170f, 0.235f, 1.0f);
const D2D1::ColorF kCardHoverBottom(0.085f, 0.110f, 0.180f, 1.0f);
const D2D1::ColorF kBorder(0.170f, 0.205f, 0.290f, 1.0f);
const D2D1::ColorF kText(0.91f, 0.94f, 0.98f, 1.0f);
const D2D1::ColorF kMuted(0.52f, 0.58f, 0.68f, 1.0f);
const D2D1::ColorF kAccent(0.26f, 0.82f, 0.93f, 1.0f);
const D2D1::ColorF kAccentPurple(0.62f, 0.30f, 0.96f, 1.0f);
const D2D1::ColorF kAccentDark(0.08f, 0.27f, 0.34f, 1.0f);
const D2D1::ColorF kAccentDarkPurple(0.22f, 0.12f, 0.42f, 1.0f);
const D2D1::ColorF kGreen(0.32f, 0.90f, 0.57f, 1.0f);
const D2D1::ColorF kAmber(1.0f, 0.69f, 0.26f, 1.0f);
const D2D1::ColorF kRed(1.0f, 0.34f, 0.40f, 1.0f);

constexpr UINT kBaseDpi = 96;

UINT windowDpi(HWND window) {
	UINT dpi = window ? GetDpiForWindow(window) : 0;
	return dpi ? dpi : kBaseDpi;
}

int scaleDipsToPixels(int dips, UINT dpi) {
	return MulDiv(dips, static_cast<int>(dpi), static_cast<int>(kBaseDpi));
}

float pixelsToDips(HWND window, int pixels) {
	return static_cast<float>(pixels) * static_cast<float>(kBaseDpi) / static_cast<float>(windowDpi(window));
}

UINT monitorFrameInterval(HWND window) {
	HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW monitorInfo{};
	monitorInfo.cbSize = sizeof(monitorInfo);
	DEVMODEW mode{};
	mode.dmSize = sizeof(mode);
	if (monitor && GetMonitorInfoW(monitor, &monitorInfo) &&
		EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
		return std::max<UINT>(1, (1000u + mode.dmDisplayFrequency - 1u) / mode.dmDisplayFrequency);
	}
	return 16;
}

float clamp01(float value) {
	return std::clamp(value, 0.0f, 1.0f);
}

float easeOutCubic(float value) {
	value = clamp01(value);
	return 1.0f - std::pow(1.0f - value, 3.0f);
}

D2D1::ColorF mixColor(D2D1::ColorF first, D2D1::ColorF second, float amount) {
	amount = clamp01(amount);
	return D2D1::ColorF(
		first.r + (second.r - first.r) * amount,
		first.g + (second.g - first.g) * amount,
		first.b + (second.b - first.b) * amount,
		first.a + (second.a - first.a) * amount);
}

D2D1::ColorF withAlpha(D2D1::ColorF color, float alpha) {
	color.a *= clamp01(alpha);
	return color;
}

std::uint32_t packGradientColor(D2D1::ColorF color) {
	auto channel = [](float value) {
		return static_cast<std::uint32_t>(std::lround(clamp01(value) * 255.0f));
	};
	return (channel(color.r) << 24) | (channel(color.g) << 16) | (channel(color.b) << 8) | channel(color.a);
}

std::uint64_t gradientKey(D2D1::ColorF first, D2D1::ColorF second) {
	return (static_cast<std::uint64_t>(packGradientColor(first)) << 32) | packGradientColor(second);
}

std::wstring keyListName(const std::vector<WORD>& keys) {
	std::wstring result;
	for (WORD key : keys) {
		if (!result.empty()) result += L" + ";
		result += displayKeyName(key);
	}
	return result;
}

std::wstring toWide(const std::string& value) {
	if (value.empty()) return {};
	int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
	std::wstring result(count, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
	return result;
}

std::string toUtf8(const std::wstring& value) {
	if (value.empty()) return {};
	int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	std::string result(count, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
	return result;
}

bool contains(NativeGui::Rect rect, float x, float y) {
	return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

bool sameSettings(const MacroSettings& a, const MacroSettings& b) {
	for (const auto& definition : settingDefinitions()) {
		if (definition.type == SettingType::Boolean) {
			if (settingBool(a, definition.id) != settingBool(b, definition.id)) return false;
		} else if (definition.type == SettingType::Integer) {
			if (settingInt(a, definition.id) != settingInt(b, definition.id)) return false;
		} else if (settingKey(a, definition.id) != settingKey(b, definition.id)) {
			return false;
		}
	}
	return true;
}

const wchar_t* labelFor(SettingId id) {
	switch (id) {
	case SettingId::BstHotkey: return L"BST macro";
	case SettingId::ThermalHotkey: return L"Thermal macro";
	case SettingId::ThermalNightVision: return L"Thermal using night vision";
	case SettingId::SnacksHotkey: return L"Snacks macro";
	case SettingId::AmmoHotkey: return L"Ammo macro";
	case SettingId::QuickTurnHotkey: return L"Quick turn";
	case SettingId::QuickTurnDegrees: return L"Turn amount";
	case SettingId::FrameGenerationMultiplier: return L"Enhanced frame generation multiplier";
	case SettingId::RpgKey: return L"RPG / heavy weapon in-game key";
	case SettingId::StickyBombKey: return L"Sticky bomb in-game key";
	case SettingId::SniperKey: return L"Sniper in-game key";
	case SettingId::PistolKey: return L"Pistol in-game key";
	case SettingId::ShotgunKey: return L"Shotgun in-game key";
	case SettingId::RifleKey: return L"Rifle in-game key";
	case SettingId::SmgKey: return L"SMG in-game key";
	case SettingId::FistsKey: return L"Fists in-game key";
	case SettingId::MeleeKey: return L"Melee weapon in-game key";
	case SettingId::RpgTabSwitchHotkey: return L"RPG / heavy weapon tab switch";
	case SettingId::StickyBombTabSwitchHotkey: return L"Sticky bomb tab switch";
	case SettingId::SniperTabSwitchHotkey: return L"Sniper tab switch";
	case SettingId::PistolTabSwitchHotkey: return L"Pistol tab switch";
	case SettingId::ShotgunTabSwitchHotkey: return L"Shotgun tab switch";
	case SettingId::RifleTabSwitchHotkey: return L"Rifle tab switch";
	case SettingId::SmgTabSwitchHotkey: return L"SMG tab switch";
	case SettingId::FistsTabSwitchHotkey: return L"Fists tab switch";
	case SettingId::MeleeTabSwitchHotkey: return L"Melee weapon tab switch";
	case SettingId::SprintKey: return L"Sprint";
	case SettingId::RpgSpamHotkey: return L"RPG spam macro";
	case SettingId::SniperSpamHotkey: return L"Sniper spam macro";
	case SettingId::DoubleSwitchHotkey: return L"Double switch macro";
	case SettingId::InteractionMenuKey: return L"Interaction menu";
	case SettingId::WeaponWheelKey: return L"Weapon wheel";
	case SettingId::ChatKey: return L"Chat";
	case SettingId::UseCursorMacros: return L"Use cursor in interaction menu";
	case SettingId::RepressLeftClick: return L"Repress left click";
	case SettingId::AutomaticLeftClickHandling: return L"Automatic left click handling";
	case SettingId::AutomaticHorizontalKeyHandling: return L"Automatic horizontal key handling";
	case SettingId::ExplicitRpgSwitchHotkey: return L"Explicit RPG switch";
	case SettingId::ExplicitHomingSwitchHotkey: return L"Explicit homing switch";
	case SettingId::ExplicitGrenadeSwitchHotkey: return L"Explicit grenade switch";
	case SettingId::SafeHeavySwapHotkey: return L"Safe heavy weapon swap";
	case SettingId::SuspendHotkey: return L"Suspend macros";
	}
	return L"Setting";
}

const wchar_t* tabName(NativeGui::Tab tab) {
	switch (tab) {
	case NativeGui::Tab::General: return L"General macros";
	case NativeGui::Tab::Weapons: return L"Weapon switching";
	case NativeGui::Tab::Controls: return L"In-game controls";
	case NativeGui::Tab::Advanced: return L"Advanced settings";
	}
	return L"Settings";
}

}

NativeGui& NativeGui::getInstance() {
	static NativeGui instance;
	return instance;
}

bool NativeGui::create(HINSTANCE appInstance, const MacroSettings& settings, ApplyCallback apply, UpdateInstallCallback updateInstall) {
	instance = appInstance;
	savedSettings = settings;
	pendingSettings = settings;
	applyCallback = std::move(apply);
	updateInstallCallback = std::move(updateInstall);

	WNDCLASSEXW windowClass{};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.hInstance = instance;
	windowClass.lpfnWndProc = windowProc;
	windowClass.lpszClassName = L"RTSSReaderMacros.NativeGui";
	windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	windowClass.hbrBackground = nullptr;
	if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

	UINT dpi = GetDpiForSystem();
	if (!dpi) dpi = kBaseDpi;
	hwnd = CreateWindowExW(WS_EX_APPWINDOW, windowClass.lpszClassName, L"RTSS Reader Macros",
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
		scaleDipsToPixels(1180, dpi), scaleDipsToPixels(760, dpi),
		nullptr, nullptr, instance, this);
	if (!hwnd) return false;
	numberEditBrush = CreateSolidBrush(RGB(10, 15, 31));
	numberEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_CENTER | ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP,
		0, 0, 1, 1, hwnd, nullptr, instance, nullptr);
	if (!numberEdit) {
		DeleteObject(numberEditBrush);
		numberEditBrush = nullptr;
		DestroyWindow(hwnd);
		hwnd = nullptr;
		return false;
	}
	SetWindowLongPtrW(numberEdit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	numberEditDefaultProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(numberEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(numberEditProc)));
	SendMessageW(numberEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(12, 30));
	updateNumberEditFont();
	ShowWindow(numberEdit, SW_HIDE);

	BOOL darkMode = TRUE;
	DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));
	tray.cbSize = sizeof(tray);
	tray.hWnd = hwnd;
	tray.uID = 1;
	tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	tray.uCallbackMessage = WM_APP_TRAY;
	tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	wcscpy_s(tray.szTip, L"RTSS Reader Macros");
	trayAdded = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
	show();
	return true;
}

void NativeGui::updateNumberEditFont() {
	if (!numberEdit) return;
	if (numberEditFont) DeleteObject(numberEditFont);
	const int height = -MulDiv(16, static_cast<int>(windowDpi(hwnd)), static_cast<int>(kBaseDpi));
	numberEditFont = CreateFontW(height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	SendMessageW(numberEdit, WM_SETFONT, reinterpret_cast<WPARAM>(numberEditFont), TRUE);
}

void NativeGui::show() {
	if (!hwnd) return;
	lastAnimationTick = GetTickCount64();
	SetTimer(hwnd, kAnimationTimer, monitorFrameInterval(hwnd), nullptr);
	ShowWindow(hwnd, SW_SHOWNORMAL);
	SetForegroundWindow(hwnd);
	UpdateWindow(hwnd);
}

void NativeGui::hide() {
	if (!hwnd) return;
	KillTimer(hwnd, kAnimationTimer);
	ShowWindow(hwnd, SW_HIDE);
}

void NativeGui::exit() {
	allowDestroy = true;
	if (hwnd) {
		KillTimer(hwnd, kMessageTimer);
		KillTimer(hwnd, kAnimationTimer);
		DestroyWindow(hwnd);
	}
}

void NativeGui::postRtssStatus(const RtssStatus& status) {
	{
		std::lock_guard lock(asyncMutex);
		pendingRtssStatus = status;
	}
	if (hwnd && IsWindow(hwnd)) PostMessageW(hwnd, WM_APP_STATUS, 0, 0);
}

void NativeGui::postUpdateCheck(const Updater::UpdateInfo& info) {
	{
		std::lock_guard lock(asyncMutex);
		pendingUpdateInfo = info;
	}
	if (hwnd && IsWindow(hwnd)) PostMessageW(hwnd, WM_APP_UPDATE_CHECK, 0, 0);
}

void NativeGui::postUpdateInstallResult(const Updater::InstallResult& result) {
	{
		std::lock_guard lock(asyncMutex);
		pendingUpdateResult = result;
	}
	if (hwnd && IsWindow(hwnd)) PostMessageW(hwnd, WM_APP_UPDATE_RESULT, 0, 0);
}

bool NativeGui::createDeviceResources() {
	if (renderTarget) return true;
	if (!d2dFactory && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf()))) return false;
	if (!writeFactory && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(writeFactory.GetAddressOf())))) return false;
	RECT client{};
	GetClientRect(hwnd, &client);
	if (FAILED(d2dFactory->CreateHwndRenderTarget(
		D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE)),
		D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(std::max(1L, client.right), std::max(1L, client.bottom)), D2D1_PRESENT_OPTIONS_NONE), &renderTarget))) return false;
	if (FAILED(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &solidBrush))) return false;
	std::array<std::uint32_t, 64> ditherPixels{};
	constexpr std::array<std::uint8_t, 64> ditherPattern = {
		0, 1, 0, 1, 1, 0, 1, 0,
		1, 0, 1, 0, 0, 1, 0, 1,
		0, 1, 1, 0, 0, 1, 1, 0,
		1, 0, 0, 1, 1, 0, 0, 1,
		1, 0, 1, 0, 0, 1, 0, 1,
		0, 1, 0, 1, 1, 0, 1, 0,
		1, 0, 0, 1, 1, 0, 0, 1,
		0, 1, 1, 0, 0, 1, 1, 0,
	};
	for (size_t i = 0; i < ditherPixels.size(); ++i) {
		const std::uint32_t alpha = 6;
		const std::uint32_t channel = ditherPattern[i] ? alpha : 0;
		ditherPixels[i] = (alpha << 24) | (channel << 16) | (channel << 8) | channel;
	}
	if (FAILED(renderTarget->CreateBitmap(
		D2D1::SizeU(8, 8), ditherPixels.data(), 8 * sizeof(std::uint32_t),
		D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &ditherBitmap))) return false;
	if (FAILED(renderTarget->CreateBitmapBrush(
		ditherBitmap.Get(),
		D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
		D2D1::BrushProperties(1.0f, D2D1::IdentityMatrix()), &ditherBrush))) return false;
	UINT dpi = windowDpi(hwnd);
	renderTarget->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
	writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &textFormat);
	writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &smallFormat);
	writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, 27.0f, L"en-us", &titleFormat);
	writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-us", &navFormat);
	for (IDWriteTextFormat* format : {textFormat.Get(), smallFormat.Get(), titleFormat.Get(), navFormat.Get()}) {
		format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
		format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
	}
	return true;
}

void NativeGui::discardDeviceResources() {
	gradientBrushes.clear();
	ditherBrush.Reset();
	ditherBitmap.Reset();
	solidBrush.Reset();
	renderTarget.Reset();
}

void NativeGui::openModal(ModalKind kind) {
	modalKind = kind;
	modalProgress = 0.0f;
	modalTarget = 1.0f;
	if (kind == ModalKind::Capture) capturePreviewProgress = 0.0f;
	if (hwnd) {
		lastAnimationTick = GetTickCount64();
		SetTimer(hwnd, kAnimationTimer, monitorFrameInterval(hwnd), nullptr);
		InvalidateRect(hwnd, nullptr, FALSE);
	}
}

void NativeGui::advanceAnimations() {
	ULONGLONG now = GetTickCount64();
	if (!lastAnimationTick) lastAnimationTick = now;
	float delta = std::clamp(static_cast<float>(now - lastAnimationTick) / 1000.0f, 0.0f, 0.05f);
	lastAnimationTick = now;
	bool changed = false;

	auto approach = [&](float& value, float target, float speed) {
		float previous = value;
		value += (target - value) * (1.0f - std::exp(-speed * delta));
		if (std::abs(target - value) < 0.002f) value = target;
		changed = changed || std::abs(previous - value) > 0.0005f;
	};

	approach(scrollOffset, targetScrollOffset, 14.0f);
	approach(hoverProgress, hoverHit >= 0 ? 1.0f : 0.0f, 16.0f);
	for (const auto& definition : settingDefinitions()) {
		if (definition.type != SettingType::Boolean) continue;
		const int key = static_cast<int>(definition.id);
		const float target = settingBool(pendingSettings, definition.id) ? 1.0f : 0.0f;
		auto it = booleanProgress.find(key);
		if (it == booleanProgress.end()) {
			booleanProgress.emplace(key, target);
			continue;
		}
		approach(it->second, target, 18.0f);
	}
	if (pageProgress < 1.0f) {
		float previous = pageProgress;
		pageProgress = std::min(1.0f, pageProgress + delta * 0.95f);
		changed = changed || previous != pageProgress;
	}
	if (modalKind != ModalKind::None) {
		float previous = modalProgress;
		float step = delta * (modalTarget > 0.0f ? 5.0f : 3.5f);
		if (modalTarget > modalProgress) modalProgress = std::min(modalTarget, modalProgress + step);
		else modalProgress = std::max(modalTarget, modalProgress - step);
		changed = changed || previous != modalProgress;
		bool hasCapturePreview = false;
		if (modalKind == ModalKind::Capture) {
			std::lock_guard lock(captureMutex);
			hasCapturePreview = !capturedChord.empty() || !captureModifiers.empty();
		}
		approach(capturePreviewProgress, hasCapturePreview ? 1.0f : 0.0f, 12.0f);
		if (modalTarget == 0.0f && modalProgress <= 0.0f) {
			modalProgress = 0.0f;
			modalKind = ModalKind::None;
		}
	}

	if (changed) InvalidateRect(hwnd, nullptr, FALSE);
}

void NativeGui::fill(Rect rect, D2D1::ColorF color, float radius) {
	color.a *= drawOpacity;
	solidBrush->SetColor(color);
	if (radius > 0.0f) renderTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), radius, radius), solidBrush.Get());
	else renderTarget->FillRectangle(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), solidBrush.Get());
}

void NativeGui::fillGradient(Rect rect, D2D1::ColorF first, D2D1::ColorF second, float radius) {
	first.a *= drawOpacity;
	second.a *= drawOpacity;
	const std::uint64_t key = gradientKey(first, second);
	auto it = gradientBrushes.find(key);
	if (it == gradientBrushes.end()) {
		D2D1_GRADIENT_STOP stops[] = {
			{0.0f, first},
			{0.28f, mixColor(first, second, 0.18f)},
			{0.56f, mixColor(first, second, 0.52f)},
			{0.82f, mixColor(first, second, 0.82f)},
			{1.0f, second},
		};
		ComPtr<ID2D1GradientStopCollection> collection;
		if (FAILED(renderTarget->CreateGradientStopCollection(stops, 5, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection))) return;
		D2D1_BRUSH_PROPERTIES brushProperties{};
		brushProperties.opacity = 1.0f;
		brushProperties.transform = D2D1::Matrix3x2F::Identity();
		ComPtr<ID2D1LinearGradientBrush> brush;
		if (FAILED(renderTarget->CreateLinearGradientBrush(
			D2D1::LinearGradientBrushProperties(D2D1::Point2F(rect.left, rect.top), D2D1::Point2F(rect.right, rect.top)),
			brushProperties, collection.Get(), &brush))) return;
		it = gradientBrushes.emplace(key, std::move(brush)).first;
	}
	ID2D1LinearGradientBrush* brush = it->second.Get();
	brush->SetStartPoint(D2D1::Point2F(rect.left, rect.top));
	brush->SetEndPoint(D2D1::Point2F(rect.right, rect.top));
	if (radius > 0.0f) renderTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), radius, radius), brush);
	else renderTarget->FillRectangle(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), brush);
	if (ditherBrush && std::max(first.a, second.a) > 0.01f) {
		ditherBrush->SetOpacity(std::max(first.a, second.a) * 0.7f);
		if (radius > 0.0f) renderTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), radius, radius), ditherBrush.Get());
		else renderTarget->FillRectangle(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), ditherBrush.Get());
		ditherBrush->SetOpacity(1.0f);
	}
}

void NativeGui::stroke(Rect rect, D2D1::ColorF color, float radius, float width) {
	color.a *= drawOpacity;
	solidBrush->SetColor(color);
	if (radius > 0.0f) renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), radius, radius), solidBrush.Get(), width);
	else renderTarget->DrawRectangle(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), solidBrush.Get(), width);
}

void NativeGui::drawText(const std::wstring& text, Rect rect, IDWriteTextFormat* format, D2D1::ColorF color) {
	if (text.empty() || !format) return;
	color.a *= drawOpacity;
	solidBrush->SetColor(color);
	renderTarget->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format,
		D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), solidBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void NativeGui::addHit(Rect rect, HitType type, SettingId setting, size_t index, Tab tab) {
	hits.push_back({rect, type, setting, index, tab});
}

void NativeGui::drawSidebar(float height) {
	fillGradient({0, 0, 258, height}, kSidebarTop, kSidebarBottom);
	fillGradient({0, 0, 258, 5}, kAccent, kAccentPurple);
	fillGradient({28, 32, 72, 76}, kAccentDark, kAccentDarkPurple, 12);
	drawText(L"R", {42, 38, 62, 68}, titleFormat.Get(), kAccent);
	drawText(L"RTSS Reader", {88, 31, 240, 56}, navFormat.Get(), kText);
	drawText(L"MACROS CONTROL CENTER", {89, 55, 240, 76}, smallFormat.Get(), kMuted);

	const std::array<std::pair<Tab, const wchar_t*>, 4> tabs = {{{Tab::General, L"General macros"}, {Tab::Weapons, L"Weapon switching"}, {Tab::Controls, L"In-game controls"}, {Tab::Advanced, L"Advanced settings"}}};
	float y = 128.0f;
	for (const auto& [tab, label] : tabs) {
		Rect row{18, y, 240, y + 48};
		int hitIndex = static_cast<int>(hits.size());
		bool hovered = hoverHit == hitIndex;
		if (tab == activeTab) fillGradient(row, kAccentDark, kAccentDarkPurple, 10);
		else if (hovered) fillGradient(row, mixColor(kSidebar, kCardTop, hoverProgress), kCardBottom, 10);
		if (tab == activeTab) fillGradient({18, y + 9, 22, y + 39}, kAccent, kAccentPurple, 2);
		drawText(label, {42, y + 13, 236, y + 37}, navFormat.Get(), tab == activeTab ? kText : hovered ? mixColor(kMuted, kText, hoverProgress) : kMuted);
		addHit(row, HitType::Tab, SettingId::BstHotkey, 0, tab);
		y += 58.0f;
	}

	float statusY = height - 92.0f;
	fillGradient({24, statusY, 234, statusY + 1}, kAccentDark, kAccentPurple);
	D2D1::ColorF dot = rtssStatus.ready() ? kGreen : rtssStatus.state == RtssState::RtssUnavailable ? kRed : kAmber;
	fill({28, statusY + 24, 40, statusY + 36}, dot, 6);
	drawText(L"RTSS STATUS", {52, statusY + 17, 220, statusY + 34}, smallFormat.Get(), kMuted);
	std::wstring status = rtssStatus.ready() ? L"Connected" : rtssStatus.state == RtssState::WaitingForGta ? L"Waiting for GTA" : rtssStatus.state == RtssState::GameNotRegistered ? L"Waiting for RTSS" : L"Unavailable";
	drawText(status, {52, statusY + 36, 226, statusY + 59}, navFormat.Get(), rtssStatus.ready() ? kGreen : dot);
}

void NativeGui::drawPage(float width, float height) {
	bool dirty = !sameSettings(savedSettings, pendingSettings);
	float pageFade = easeOutCubic(clamp01(pageProgress / 0.24f));
	float pageSlide = (1.0f - pageFade) * 28.0f;
	const wchar_t* title = tabName(activeTab);
	drawText(title, {294 - pageSlide, 32, width - 390 - pageSlide, 72}, titleFormat.Get(), withAlpha(kText, pageFade));
	drawText(L"Configure the way your macros behave in-game.", {296 - pageSlide, 77, width - 390 - pageSlide, 101}, textFormat.Get(), withAlpha(kMuted, pageFade));

	Rect statusRect{width - 350, 38, width - 32, 78};
	D2D1::ColorF statusColor = rtssStatus.ready() ? kGreen : rtssStatus.state == RtssState::RtssUnavailable ? kRed : kAmber;
	fillGradient(statusRect, kCardTop, kCardBottom, 18);
	float statusPulse = rtssStatus.ready() ? 1.0f : 0.72f + 0.28f * std::sin(static_cast<float>(GetTickCount64() % 1800) / 1800.0f * 6.2831853f);
	fill({statusRect.left + 14, statusRect.top + 14, statusRect.left + 24, statusRect.top + 24}, withAlpha(statusColor, statusPulse), 5);
	std::wstring status = toWide(rtssStatus.message.empty() ? "RTSS status pending" : rtssStatus.message);
	if (status.size() > 36) status = status.substr(0, 33) + L"...";
	drawText(status, {statusRect.left + 34, statusRect.top + 9, statusRect.right - 10, statusRect.bottom - 7}, smallFormat.Get(), kText);

	if (!message.empty()) {
		drawText(toWide(message), {296, 106, width - 32, 128}, smallFormat.Get(), messageError ? kRed : kAccent);
	}
	fillGradient({294, 132, width - 32, 134}, kAccent, kAccentPurple);

	static const SettingRow rows[] = {
		{SettingId::BstHotkey, Tab::General, L"BST macro", L"Gets BST from the CEO menu"},
		{SettingId::ThermalHotkey, Tab::General, L"Thermal macro", L"Toggles thermal vision if equipped."},
		{SettingId::ThermalNightVision, Tab::General, L"Toggle thermal using night vision", L"Requires the telescope glitch night vision bullshit thing."},
		{SettingId::SnacksHotkey, Tab::General, L"Snacks macro", L"Automatically open snacks menu"},
		{SettingId::AmmoHotkey, Tab::General, L"Ammo macro", L"Refill ammo"},
		{SettingId::QuickTurnHotkey, Tab::General, L"Quick turn", L"Turn your character by the configured number of degrees. Requires raw input and 0 mouse sensitivity in GTA."},
		{SettingId::QuickTurnDegrees, Tab::General, L"Turn amount", L"How many degrees the quick-turn macro should rotate."},
		{SettingId::UseCursorMacros, Tab::General, L"Use cursor in interaction menu", L"Use cursor to navigate faster for some macros. Requires 9/10 safezone size and a 16:9 of wider aspect ratio."},
		{SettingId::RpgSpamHotkey, Tab::Weapons, L"RPG spam macro", L"Switch through sticky bomb to the RPG."},
		{SettingId::SniperSpamHotkey, Tab::Weapons, L"Sniper spam macro", L"Switch through sticky bomb to the sniper."},
		{SettingId::DoubleSwitchHotkey, Tab::Weapons, L"Double switch macro", L"Presses the heavy weapon keybind twice to allow for faster switching between heavy weapons."},
		{SettingId::RpgTabSwitchHotkey, Tab::Weapons, L"RPG / heavy weapon tab switch", L"Macro hotkey that switches to this weapon."},
		{SettingId::StickyBombTabSwitchHotkey, Tab::Weapons, L"Sticky bomb tab switch", L"Macro hotkey that switches to this weapon."},
		{SettingId::SniperTabSwitchHotkey, Tab::Weapons, L"Sniper tab switch", L"Macro hotkey that switches to this weapon."},
		{SettingId::PistolTabSwitchHotkey, Tab::Weapons, L"Pistol tab switch", L"Macro hotkey that switches to this weapon."},
		{SettingId::ShotgunTabSwitchHotkey, Tab::Weapons, L"Shotgun tab switch", L"Macro hotkey that switches to this weapon."},
		{SettingId::RifleTabSwitchHotkey, Tab::Weapons, L"Rifle tab switch", L"Macro hotkey that switches to this weapon."},
		{SettingId::SmgTabSwitchHotkey, Tab::Weapons, L"SMG tab switch", L"Macro hotkey that switches to this weapon."},
		{SettingId::FistsTabSwitchHotkey, Tab::Weapons, L"Fists tab switch", L"Macro hotkey that switches to this weapon."},
		{SettingId::MeleeTabSwitchHotkey, Tab::Weapons, L"Melee weapon tab switch", L"Macro hotkey that switches to this weapon."},
		{SettingId::RpgKey, Tab::Controls, L"RPG / heavy weapon in-game key", L"The GTA key used to select this weapon."},
		{SettingId::StickyBombKey, Tab::Controls, L"Sticky bomb in-game key", L"The GTA key used to select this weapon."},
		{SettingId::SniperKey, Tab::Controls, L"Sniper in-game key", L"The GTA key used to select this weapon."},
		{SettingId::PistolKey, Tab::Controls, L"Pistol in-game key", L"The GTA key used to select this weapon."},
		{SettingId::ShotgunKey, Tab::Controls, L"Shotgun in-game key", L"The GTA key used to select this weapon."},
		{SettingId::RifleKey, Tab::Controls, L"Rifle in-game key", L"The GTA key used to select this weapon."},
		{SettingId::SmgKey, Tab::Controls, L"SMG in-game key", L"The GTA key used to select this weapon."},
		{SettingId::FistsKey, Tab::Controls, L"Fists in-game key", L"The GTA key used to select this weapon."},
		{SettingId::MeleeKey, Tab::Controls, L"Melee weapon in-game key", L"The GTA key used to select this weapon."},
		{SettingId::SprintKey, Tab::Controls, L"Sprint", L"The GTA key used to sprint."},
		{SettingId::InteractionMenuKey, Tab::Controls, L"Interaction menu", L"The GTA key used to open the interaction menu."},
		{SettingId::WeaponWheelKey, Tab::Controls, L"Weapon wheel", L"In-game keybind for weapon wheel"},
		{SettingId::ChatKey, Tab::Controls, L"Chat", L"Open chat and suspend macros while typing."},
		{SettingId::RepressLeftClick, Tab::Advanced, L"Repress left click", L"Restore left click if it was held before the macro started."},
		{SettingId::AutomaticLeftClickHandling, Tab::Advanced, L"Automatic left click handling", L"Automatically release and restore left click when shift switching weapons."},
		{SettingId::AutomaticHorizontalKeyHandling, Tab::Advanced, L"Automatic horizontal key handling", L"Automatically release and restore A/D when shift switching weapons."},
		{SettingId::FrameGenerationMultiplier, Tab::Advanced, L"Frame generation multiplier", L"For compatbility with Frame Generation on Enhanced. Macros may still be more buggy."},
		{SettingId::ExplicitRpgSwitchHotkey, Tab::Advanced, L"Explicit RPG switch", L"Guarantees a switch to RPG if your weapon loadout has the RPG in the first heavy-weapon slot."},
		{SettingId::ExplicitHomingSwitchHotkey, Tab::Advanced, L"Explicit homing switch", L"Guarantees a switch to homing launcher if your weapon loadout has it in the second heavy-weapon slot."},
		{SettingId::ExplicitGrenadeSwitchHotkey, Tab::Advanced, L"Explicit grenade switch", L"Guarantees a switch to grenade launcher if your weapon loadout has it in the third heavy-weapon slot."},
		{SettingId::SafeHeavySwapHotkey, Tab::Advanced, L"Safe heavy weapon swap", L"Prevents your currently held heavy weapon from being reset when switching to it after respawning."},
		{SettingId::SuspendHotkey, Tab::Advanced, L"Suspend macros", L"Temporarily prevents macro hotkeys from triggering. Press it again to resume."},
	};

	std::vector<const SettingRow*> visible;
	for (const SettingRow& row : rows) if (row.tab == activeTab) visible.push_back(&row);
	const float contentTop = 142.0f;
	const float contentBottom = height - 82.0f;
	const float left = 294.0f;
	const float gap = 16.0f;
	const float cardWidth = (width - left - 32.0f - gap) / 2.0f;
	const float cardHeight = 112.0f;
	const float rowGap = 12.0f;
	const float maxScroll = std::max(0.0f, ((visible.size() + 1) / 2.0f) * (cardHeight + rowGap) - (contentBottom - contentTop));
	targetScrollOffset = std::clamp(targetScrollOffset, 0.0f, maxScroll);
	scrollOffset = std::clamp(scrollOffset, 0.0f, maxScroll);

	renderTarget->PushAxisAlignedClip(D2D1::RectF(258.0f, contentTop, width - 24.0f, contentBottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	for (size_t i = 0; i < visible.size(); ++i) {
		float column = static_cast<float>(i % 2);
		float row = static_cast<float>(i / 2);
		float x = left + column * (cardWidth + gap);
		float reveal = clamp01((pageProgress - static_cast<float>(i) * 0.04f) / 0.22f);
		float cardProgress = easeOutCubic(reveal);
		float slide = (1.0f - cardProgress) * 36.0f;
		float y = contentTop + row * (cardHeight + rowGap) - scrollOffset;
		float drawX = x - slide;
		Rect card{drawX, y, drawX + cardWidth, y + cardHeight};
		int cardHitIndex = static_cast<int>(hits.size());
		bool hovered = hoverHit == cardHitIndex;
		float hoverAmount = hovered ? hoverProgress : 0.0f;
		fillGradient(card, withAlpha(mixColor(kCardTop, kCardHoverTop, hoverAmount), cardProgress),
			withAlpha(mixColor(kCardBottom, kCardHoverBottom, hoverAmount), cardProgress), 12);
		stroke(card, withAlpha(kBorder, cardProgress), 12);
		drawText(visible[i]->label, {drawX + 18, y + 14, drawX + cardWidth - 20, y + 35}, navFormat.Get(), withAlpha(kText, cardProgress));
		drawText(visible[i]->description, {drawX + 18, y + 43, drawX + cardWidth - 32, y + 94}, smallFormat.Get(), withAlpha(kMuted, cardProgress));
		Rect control{drawX + cardWidth - 148, y + 14, drawX + cardWidth - 18, y + 42};
		int controlHitIndex = static_cast<int>(hits.size());
		bool controlHovered = hoverHit == controlHitIndex;
		if (settingIsBoolean(visible[i]->id)) {
			const int booleanKey = static_cast<int>(visible[i]->id);
			const float target = settingBool(pendingSettings, visible[i]->id) ? 1.0f : 0.0f;
			auto [progressIt, inserted] = booleanProgress.emplace(booleanKey, target);
			if (!inserted) progressIt->second = clamp01(progressIt->second);
			const float toggleProgress = progressIt->second;
			D2D1::ColorF trackStart = mixColor(kBorder, kAccentDark, toggleProgress);
			D2D1::ColorF trackEnd = mixColor(kCardBottom, kAccentPurple, toggleProgress);
			fillGradient({control.right - 52, control.top + 2, control.right, control.top + 26}, withAlpha(trackStart, cardProgress), withAlpha(trackEnd, cardProgress), 12);
			float thumbLeft = control.right - 48.0f + toggleProgress * 22.0f;
			fill({thumbLeft, control.top + 6, thumbLeft + 20.0f, control.top + 22}, withAlpha(mixColor(kMuted, kAccent, toggleProgress), cardProgress), 8);
		} else if (visible[i]->id == SettingId::FrameGenerationMultiplier) {
			const float sliderProgress = static_cast<float>(std::clamp(settingInt(pendingSettings, visible[i]->id), 1, 4) - 1) / 3.0f;
			fillGradient(control, withAlpha(kAccentDark, cardProgress), withAlpha(kAccentDarkPurple, cardProgress), 8);
			const Rect track{control.left + 10, control.top + 12, control.right - 42, control.top + 17};
			fill(track, withAlpha(kBorder, cardProgress), 3);
			if (sliderProgress > 0.0f) {
				fillGradient({track.left, track.top, track.left + (track.right - track.left) * sliderProgress, track.bottom},
					withAlpha(kAccent, cardProgress), withAlpha(kAccentPurple, cardProgress), 3);
			}
			for (int value = 1; value <= 4; ++value) {
				const float tick = track.left + (track.right - track.left) * static_cast<float>(value - 1) / 3.0f;
				fill({tick - 2, track.top - 2, tick + 2, track.bottom + 2}, withAlpha(value <= settingInt(pendingSettings, visible[i]->id) ? kAccent : kMuted, cardProgress), 2);
			}
			const float thumb = track.left + (track.right - track.left) * sliderProgress;
			fill({thumb - 7, track.top - 6, thumb + 7, track.bottom + 6}, withAlpha(kText, cardProgress), 7);
			drawText(std::to_wstring(settingInt(pendingSettings, visible[i]->id)) + L"x",
				{control.right - 38, control.top + 3, control.right - 4, control.bottom - 3}, navFormat.Get(),
				withAlpha(kText, cardProgress));
		} else if (settingIsInteger(visible[i]->id)) {
			fillGradient(control, withAlpha(controlHovered ? mixColor(kAccentDark, kAccent, hoverProgress) : kAccentDark, cardProgress),
				withAlpha(controlHovered ? kAccentPurple : kAccentDarkPurple, cardProgress), 8);
			std::wstring number = std::to_wstring(settingInt(pendingSettings, visible[i]->id));
			if (visible[i]->id == SettingId::QuickTurnDegrees) number += L"\u00B0";
			drawText(number,
				{control.left + 10, control.top + 5, control.right - 8, control.bottom - 3}, smallFormat.Get(),
				withAlpha(controlHovered ? kText : kAccent, cardProgress));
		} else {
			fillGradient(control, withAlpha(controlHovered ? mixColor(kAccentDark, kAccent, hoverProgress) : kAccentDark, cardProgress),
				withAlpha(controlHovered ? kAccentPurple : kAccentDarkPurple, cardProgress), 8);
			drawText(settingKey(pendingSettings, visible[i]->id).displayName(), {control.left + 10, control.top + 5, control.right - 8, control.bottom - 3}, smallFormat.Get(), withAlpha(controlHovered ? kText : kAccent, cardProgress));
		}
		addHit(control, HitType::Setting, visible[i]->id);
	}
	renderTarget->PopAxisAlignedClip();

	if (maxScroll > 0.0f) {
		float trackHeight = contentBottom - contentTop;
		float thumbHeight = std::max(28.0f, trackHeight * trackHeight / (trackHeight + maxScroll));
		float thumbTop = contentTop + (trackHeight - thumbHeight) * (scrollOffset / maxScroll);
		fillGradient({width - 18, thumbTop, width - 12, thumbTop + thumbHeight}, kAccent, kAccentPurple, 3);
	}

	fillGradient({258, height - 70, width, height}, kSidebarTop, kSidebarBottom);
	if (activeTab == Tab::Weapons) {
		float matchButtonRight = std::min(width - 530.0f, 594.0f);
		Rect matchButton{294, height - 54, matchButtonRight, height - 20};
		int matchIndex = static_cast<int>(hits.size());
		bool hovered = hoverHit == matchIndex;
		fillGradient(matchButton, hovered ? mixColor(kAccentDark, kAccent, hoverProgress) : kAccentDark,
			hovered ? kAccentPurple : kAccentDarkPurple, 8);
		drawText(L"Match tab switch keybinds to GTA keybinds", {matchButton.left + 13, matchButton.top + 8, matchButton.right - 8, matchButton.bottom}, smallFormat.Get(), kText);
		addHit(matchButton, HitType::MatchGtaKeys);
	} else {
		drawText(dirty ? L"Unsaved changes" : L"All changes saved", {294, height - 47, width - 520, height - 22}, smallFormat.Get(), dirty ? kAmber : kMuted);
	}
	Rect discardButton{width - 500, height - 54, width - 396, height - 20};
	Rect importButton{width - 392, height - 54, width - 232, height - 20};
	Rect hideButton{width - 228, height - 54, width - 138, height - 20};
	Rect saveButton{width - 118, height - 54, width - 32, height - 20};
	int discardIndex = static_cast<int>(hits.size());
	bool discardHovered = hoverHit == discardIndex;
	fillGradient(discardButton, discardHovered ? mixColor(kCardTop, kCardHoverTop, hoverProgress) : dirty ? kCardHoverTop : kCardTop,
		discardHovered ? mixColor(kCardBottom, kCardHoverBottom, hoverProgress) : dirty ? kCardHoverBottom : kCardBottom, 8);
	drawText(L"Discard", {discardButton.left + 23, discardButton.top + 8, discardButton.right - 10, discardButton.bottom}, smallFormat.Get(), dirty ? kText : kMuted);
	addHit(discardButton, HitType::Discard);
	int importIndex = static_cast<int>(hits.size());
	bool importHovered = hoverHit == importIndex;
	fillGradient(importButton, importHovered ? mixColor(kCardTop, kCardHoverTop, hoverProgress) : kCardTop,
		importHovered ? mixColor(kCardBottom, kCardHoverBottom, hoverProgress) : kCardBottom, 8);
	drawText(L"Import GTA Keybinds", {importButton.left + 18, importButton.top + 8, importButton.right - 8, importButton.bottom}, smallFormat.Get(), kText);
	addHit(importButton, HitType::Import);
	int hideIndex = static_cast<int>(hits.size());
	bool hideHovered = hoverHit == hideIndex;
	fillGradient(hideButton, hideHovered ? mixColor(kCardTop, kCardHoverTop, hoverProgress) : kCardTop,
		hideHovered ? mixColor(kCardBottom, kCardHoverBottom, hoverProgress) : kCardBottom, 8);
	drawText(L"Hide", {hideButton.left + 26, hideButton.top + 8, hideButton.right - 8, hideButton.bottom}, smallFormat.Get(), kText);
	addHit(hideButton, HitType::Hide);
	int saveIndex = static_cast<int>(hits.size());
	bool saveHovered = hoverHit == saveIndex;
	fillGradient(saveButton, saveHovered ? mixColor(dirty ? kAccent : kBorder, kAccent, hoverProgress) : dirty ? kAccent : kBorder,
		saveHovered ? kAccentPurple : dirty ? kAccentPurple : kCardBottom, 8);
	drawText(L"Save", {saveButton.left + 27, saveButton.top + 8, saveButton.right - 10, saveButton.bottom}, smallFormat.Get(), dirty ? kBackground : kMuted);
	addHit(saveButton, HitType::Save);
}

void NativeGui::drawCaptureModal(float width, float height) {
	Rect panel{width / 2.0f - 260, height / 2.0f - 175, width / 2.0f + 260, height / 2.0f + 175};
	fillGradient(panel, kCardTop, kCardBottom, 18);
	stroke(panel, kBorder, 18, 1.5f);
	drawText(L"HOTKEY CAPTURE", {panel.left + 30, panel.top + 28, panel.right - 30, panel.top + 52}, smallFormat.Get(), kAccent);
	drawText(L"Press the key you want to use", {panel.left + 30, panel.top + 70, panel.right - 30, panel.top + 105}, titleFormat.Get(), kText);
	drawText(labelFor(captureSetting), {panel.left + 30, panel.top + 116, panel.right - 30, panel.top + 142}, textFormat.Get(), kMuted);
	Rect inputArea{panel.left + 30, panel.top + 158, panel.right - 30, panel.top + 220};
	fillGradient(inputArea, kSidebarTop, kSidebarBottom, 12);
	std::vector<WORD> modifiers;
	KeyChord captured;
	{
		std::lock_guard lock(captureMutex);
		modifiers = captureModifiers;
		captured = capturedChord;
	}
	std::wstring preview = captured.empty() ? keyListName(modifiers) : captured.displayName();
	if (preview.empty()) {
		drawText(L"Press a key...", {inputArea.left + 22, inputArea.top + 17, inputArea.right - 22, inputArea.bottom - 12}, navFormat.Get(), kMuted);
		float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(GetTickCount64() % 1400) / 1400.0f * 6.2831853f);
		fill({inputArea.left + 9, inputArea.top + 24, inputArea.left + 15, inputArea.top + 30}, withAlpha(kAccent, pulse), 3);
	} else {
		float progress = easeOutCubic(capturePreviewProgress);
		Rect chip{inputArea.left + 22.0f + (1.0f - progress) * 18.0f, inputArea.top + 12, inputArea.right - 22, inputArea.bottom - 12};
		fillGradient(chip, withAlpha(kAccentDark, progress), withAlpha(kAccentPurple, progress), 9);
		stroke(chip, withAlpha(kAccent, progress * 0.75f), 9);
		drawText(preview, {chip.left + 16, chip.top + 5, chip.right - 16, chip.bottom - 3}, navFormat.Get(), withAlpha(kText, progress));
	}
	Rect unbind{panel.left + 30, panel.bottom - 60, panel.left + 150, panel.bottom - 24};
	Rect cancel{panel.right - 150, panel.bottom - 60, panel.right - 30, panel.bottom - 24};
	int unbindIndex = static_cast<int>(hits.size());
	bool unbindHovered = hoverHit == unbindIndex;
	fillGradient(unbind, unbindHovered ? mixColor(kCardTop, kCardHoverTop, hoverProgress) : kCardTop,
		unbindHovered ? mixColor(kCardBottom, kCardHoverBottom, hoverProgress) : kCardBottom, 8);
	drawText(L"Unbind", {unbind.left + 30, unbind.top + 9, unbind.right, unbind.bottom}, smallFormat.Get(), kText);
	addHit(unbind, HitType::CaptureUnbind);
	int cancelIndex = static_cast<int>(hits.size());
	bool cancelHovered = hoverHit == cancelIndex;
	fillGradient(cancel, cancelHovered ? mixColor(kAccentDark, kAccent, hoverProgress) : kAccentDark,
		cancelHovered ? kAccentPurple : kAccentDarkPurple, 8);
	drawText(L"Cancel", {cancel.left + 31, cancel.top + 9, cancel.right, cancel.bottom}, smallFormat.Get(), kText);
	addHit(cancel, HitType::CaptureCancel);
}

void NativeGui::drawNumberModal(float width, float height) {
	Rect panel{width / 2.0f - 260, height / 2.0f - 155, width / 2.0f + 260, height / 2.0f + 155};
	fillGradient(panel, kCardTop, kCardBottom, 18);
	stroke(panel, kBorder, 18, 1.5f);
	drawText(L"QUICK TURN", {panel.left + 30, panel.top + 28, panel.right - 30, panel.top + 52}, smallFormat.Get(), kAccent);
	drawText(L"Enter turn amount", {panel.left + 30, panel.top + 70, panel.right - 30, panel.top + 105}, titleFormat.Get(), kText);
	drawText(L"Use a value from 1 to 360 degrees.", {panel.left + 30, panel.top + 116, panel.right - 30, panel.top + 142}, textFormat.Get(), kMuted);
	Rect inputArea{panel.left + 30, panel.top + 158, panel.right - 30, panel.top + 220};
	fillGradient(inputArea, kSidebarTop, kSidebarBottom, 12);
	drawText(L"\u00B0", {inputArea.right - 54, inputArea.top + 13, inputArea.right - 22, inputArea.bottom - 10}, navFormat.Get(), kAccent);
	if (numberEdit) {
		const float animationProgress = modalTarget == 0.0f ? this->modalProgress : easeOutCubic(this->modalProgress);
		const float modalOffset = modalTarget == 0.0f ? (1.0f - this->modalProgress) * 112.0f : (1.0f - animationProgress) * 24.0f;
		const Rect editArea{inputArea.left + 22, inputArea.top + 6 + modalOffset, inputArea.right - 58, inputArea.bottom - 6 + modalOffset};
		SetWindowPos(numberEdit, HWND_TOP, scaleDipsToPixels(static_cast<int>(editArea.left), windowDpi(hwnd)),
			scaleDipsToPixels(static_cast<int>(editArea.top), windowDpi(hwnd)),
			scaleDipsToPixels(static_cast<int>(editArea.right - editArea.left), windowDpi(hwnd)),
			scaleDipsToPixels(static_cast<int>(editArea.bottom - editArea.top), windowDpi(hwnd)), SWP_NOACTIVATE);
		ShowWindow(numberEdit, numberCaptureActive ? SW_SHOWNOACTIVATE : SW_HIDE);
		if (numberCaptureActive && GetFocus() != numberEdit) SetFocus(numberEdit);
	}
	Rect cancel{panel.right - 150, panel.bottom - 60, panel.right - 30, panel.bottom - 24};
	int cancelIndex = static_cast<int>(hits.size());
	bool cancelHovered = hoverHit == cancelIndex;
	fillGradient(cancel, cancelHovered ? mixColor(kAccentDark, kAccent, hoverProgress) : kAccentDark,
		cancelHovered ? kAccentPurple : kAccentDarkPurple, 8);
	drawText(L"Cancel", {cancel.left + 31, cancel.top + 9, cancel.right, cancel.bottom}, smallFormat.Get(), kText);
	addHit(cancel, HitType::CaptureCancel);
	Rect apply{panel.right - 280, panel.bottom - 60, panel.right - 160, panel.bottom - 24};
	int applyIndex = static_cast<int>(hits.size());
	bool applyHovered = hoverHit == applyIndex;
	fillGradient(apply, applyHovered ? mixColor(kAccent, kAccentPurple, hoverProgress) : kAccent,
		applyHovered ? kAccentPurple : kAccentDark, 8);
	drawText(L"Apply", {apply.left + 32, apply.top + 9, apply.right, apply.bottom}, smallFormat.Get(), kBackground);
	addHit(apply, HitType::NumberApply);
}

void NativeGui::drawUpdateModal(float width, float height) {
	Rect panel{width / 2.0f - 310, height / 2.0f - 190, width / 2.0f + 310, height / 2.0f + 190};
	fillGradient(panel, kCardTop, kCardBottom, 18);
	stroke(panel, kBorder, 18, 1.5f);
	drawText(L"SOFTWARE UPDATE", {panel.left + 30, panel.top + 28, panel.right - 30, panel.top + 54}, smallFormat.Get(), kAccent);
	drawText(L"A new build is ready", {panel.left + 30, panel.top + 66, panel.right - 30, panel.top + 106}, titleFormat.Get(), kText);
	drawText(L"Download the latest version and the app will restart automatically. Saved settings stay in place.",
		{panel.left + 30, panel.top + 112, panel.right - 30, panel.top + 155}, textFormat.Get(), kMuted);

	Rect versions{panel.left + 30, panel.top + 172, panel.right - 30, panel.top + 232};
	fillGradient(versions, kSidebarTop, kSidebarBottom, 12);
	drawText(L"INSTALLED", {versions.left + 18, versions.top + 10, versions.left + 150, versions.top + 30}, smallFormat.Get(), kMuted);
	drawText(L"LATEST", {versions.left + 300, versions.top + 10, versions.right - 18, versions.top + 30}, smallFormat.Get(), kMuted);
	drawText(L"v" + toWide(updateInfo.currentVersion), {versions.left + 18, versions.top + 29, versions.left + 260, versions.bottom - 8}, navFormat.Get(), kText);
	drawText(L"v" + toWide(updateInfo.latestVersion), {versions.left + 300, versions.top + 29, versions.right - 18, versions.bottom - 8}, navFormat.Get(), kGreen);

	if (!updateStatus.empty())
		drawText(toWide(updateStatus), {panel.left + 30, panel.top + 246, panel.right - 30, panel.top + 276}, smallFormat.Get(), updateInstalling ? kAccent : kMuted);

	const bool busy = updateInstalling;
	Rect cancel{panel.right - 390, panel.bottom - 58, panel.right - 260, panel.bottom - 22};
	int cancelIndex = static_cast<int>(hits.size());
	bool cancelHovered = hoverHit == cancelIndex;
	fillGradient(cancel, cancelHovered ? mixColor(kCardTop, kCardHoverTop, hoverProgress) : kCardTop,
		cancelHovered ? mixColor(kCardBottom, kCardHoverBottom, hoverProgress) : kCardBottom, 8);
	drawText(L"Cancel", {cancel.left + 32, cancel.top + 9, cancel.right, cancel.bottom}, smallFormat.Get(), busy ? kMuted : kText);
	addHit(cancel, HitType::UpdateCancel);

	Rect install{panel.right - 245, panel.bottom - 58, panel.right - 30, panel.bottom - 22};
	int installIndex = static_cast<int>(hits.size());
	bool installHovered = hoverHit == installIndex;
	fillGradient(install, busy ? kBorder : installHovered ? mixColor(kAccent, kAccentPurple, hoverProgress) : kAccent,
		busy ? kCardBottom : installHovered ? kAccentPurple : kAccentDark, 8);
	drawText(busy ? L"Downloading..." : L"Download and restart",
		{install.left + 15, install.top + 9, install.right - 15, install.bottom}, smallFormat.Get(), busy ? kMuted : kBackground);
	addHit(install, HitType::UpdateInstall);
}

void NativeGui::drawProfileModal(float width, float height) {
	Rect panel{width / 2.0f - 360, height / 2.0f - 260, width / 2.0f + 360, height / 2.0f + 260};
	fillGradient(panel, kCardTop, kCardBottom, 18);
	stroke(panel, kBorder, 18, 1.5f);
	drawText(importEnhanced ? L"GTA5 ENHANCED PROFILES" : L"GTA5 LEGACY PROFILES", {panel.left + 30, panel.top + 27, panel.right - 30, panel.top + 55}, smallFormat.Get(), kAccent);
	drawText(L"Choose a control profile", {panel.left + 30, panel.top + 65, panel.right - 30, panel.top + 105}, titleFormat.Get(), kText);
	drawText(L"Profiles are sorted by the last modified time of control\\user.xml.", {panel.left + 30, panel.top + 109, panel.right - 30, panel.top + 135}, smallFormat.Get(), kMuted);
	float y = panel.top + 155;
	for (size_t i = 0; i < profiles.size(); ++i) {
		Rect row{panel.left + 28, y, panel.right - 28, y + 52};
		int hitIndex = static_cast<int>(hits.size());
		bool hovered = hoverHit == hitIndex;
		fillGradient(row, hovered ? mixColor(kSidebarTop, kCardHoverTop, hoverProgress) : kSidebarTop,
			hovered ? mixColor(kSidebarBottom, kCardHoverBottom, hoverProgress) : kSidebarBottom, 8);
		drawText(profiles[i].name, {row.left + 18, row.top + 8, row.left + 220, row.bottom - 8}, navFormat.Get(), kText);
		drawText(profiles[i].modified, {row.left + 240, row.top + 10, row.right - 18, row.bottom - 8}, smallFormat.Get(), kMuted);
		addHit(row, HitType::Profile, SettingId::BstHotkey, i);
		y += 60;
		if (y > panel.bottom - 78) break;
	}
	if (profiles.empty()) drawText(toWide(message), {panel.left + 30, panel.top + 165, panel.right - 30, panel.bottom - 90}, smallFormat.Get(), kRed);
	Rect cancel{panel.right - 150, panel.bottom - 58, panel.right - 30, panel.bottom - 22};
	int cancelIndex = static_cast<int>(hits.size());
	bool cancelHovered = hoverHit == cancelIndex;
	fillGradient(cancel, cancelHovered ? mixColor(kAccentDark, kAccent, hoverProgress) : kAccentDark,
		cancelHovered ? kAccentPurple : kAccentDarkPurple, 8);
	drawText(L"Cancel", {cancel.left + 31, cancel.top + 9, cancel.right, cancel.bottom}, smallFormat.Get(), kText);
	addHit(cancel, HitType::CaptureCancel);
}

void NativeGui::drawModal(float width, float height) {
	if (modalKind == ModalKind::None) return;
	const bool closing = modalTarget == 0.0f;
	float progress = closing ? modalProgress : easeOutCubic(modalProgress);
	drawOpacity = 1.0f;
	fill({0, 0, width, height}, withAlpha(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), progress * 0.68f));
	drawOpacity = progress;

	D2D1_MATRIX_3X2_F originalTransform{};
	renderTarget->GetTransform(&originalTransform);
	D2D1_MATRIX_3X2_F modalTransform = originalTransform;
	modalTransform._32 += closing ? (1.0f - modalProgress) * 112.0f : (1.0f - progress) * 24.0f;
	renderTarget->SetTransform(&modalTransform);

	if (modalKind == ModalKind::Capture) {
		drawCaptureModal(width, height);
	} else if (modalKind == ModalKind::Number) {
		drawNumberModal(width, height);
	} else if (modalKind == ModalKind::Update) {
		drawUpdateModal(width, height);
	} else if (modalKind == ModalKind::Profile) {
		drawProfileModal(width, height);
	} else if (modalKind == ModalKind::Source) {
		Rect panel{width / 2.0f - 280, height / 2.0f - 155, width / 2.0f + 280, height / 2.0f + 155};
		fillGradient(panel, kCardTop, kCardBottom, 18);
		stroke(panel, kBorder, 18, 1.5f);
		drawText(L"IMPORT GTA CONTROLS", {panel.left + 30, panel.top + 29, panel.right - 30, panel.top + 56}, smallFormat.Get(), kAccent);
		drawText(L"Which game profile should be used?", {panel.left + 30, panel.top + 70, panel.right - 30, panel.top + 112}, titleFormat.Get(), kText);
		Rect legacy{panel.left + 30, panel.top + 140, panel.left + 250, panel.top + 190};
		Rect enhanced{panel.left + 270, panel.top + 140, panel.right - 30, panel.top + 190};
		int legacyIndex = static_cast<int>(hits.size());
		bool legacyHovered = hoverHit == legacyIndex;
		fillGradient(legacy, legacyHovered ? mixColor(kAccentDark, kAccent, hoverProgress) : kAccentDark,
			legacyHovered ? kAccentPurple : kAccentDarkPurple, 10);
		drawText(L"GTA 5 Legacy", {legacy.left + 36, legacy.top + 14, legacy.right, legacy.bottom}, navFormat.Get(), kText);
		addHit(legacy, HitType::SourceLegacy);
		int enhancedIndex = static_cast<int>(hits.size());
		bool enhancedHovered = hoverHit == enhancedIndex;
		fillGradient(enhanced, enhancedHovered ? mixColor(kAccentDark, kAccent, hoverProgress) : kAccentDark,
			enhancedHovered ? kAccentPurple : kAccentDarkPurple, 10);
		drawText(L"GTA5 Enhanced", {enhanced.left + 32, enhanced.top + 14, enhanced.right, enhanced.bottom}, navFormat.Get(), kText);
		addHit(enhanced, HitType::SourceEnhanced);
		Rect cancel{panel.left + 30, panel.bottom - 52, panel.left + 150, panel.bottom - 18};
		int cancelIndex = static_cast<int>(hits.size());
		bool cancelHovered = hoverHit == cancelIndex;
		fillGradient(cancel, cancelHovered ? mixColor(kCardTop, kCardHoverTop, hoverProgress) : kCardTop,
			cancelHovered ? mixColor(kCardBottom, kCardHoverBottom, hoverProgress) : kCardBottom, 8);
		drawText(L"Cancel", {cancel.left + 31, cancel.top + 8, cancel.right, cancel.bottom}, smallFormat.Get(), kText);
		addHit(cancel, HitType::CaptureCancel);
	}
	renderTarget->SetTransform(&originalTransform);
	drawOpacity = 1.0f;
}

void NativeGui::paint() {
	if (!createDeviceResources()) return;
	D2D1_SIZE_F size = renderTarget->GetSize();
	float width = size.width;
	float height = size.height;
	hits.clear();
	renderTarget->BeginDraw();
	renderTarget->Clear(kBackground);
	fillGradient({0, 0, width, height}, kBackgroundTop, kBackgroundBottom);
	drawPage(width, height);
	drawSidebar(height);
	if (modalKind != ModalKind::None) drawModal(width, height);
	HRESULT result = renderTarget->EndDraw();
	if (result == D2DERR_RECREATE_TARGET) discardDeviceResources();
	updateWindowTitle();
}

void NativeGui::updateWindowTitle() {
	if (!hwnd) return;
	std::wstring title = L"RTSS Reader Macros";
	if (sameSettings(savedSettings, pendingSettings)) title += L"  |  Saved";
	else title += L"  |  Unsaved changes";
	SetWindowTextW(hwnd, title.c_str());
}

void NativeGui::handleMouseMove(float x, float y) {
	if (frameGenerationSliderDragging) updateFrameGenerationSlider(x);
	int newHover = -1;
	for (size_t i = 0; i < hits.size(); ++i) {
		if (contains(hits[i].rect, x, y)) newHover = static_cast<int>(i);
	}
	if (newHover != hoverHit) {
		hoverHit = newHover;
		hoverProgress = 0.0f;
		InvalidateRect(hwnd, nullptr, FALSE);
	}
}

void NativeGui::updateFrameGenerationSlider(float x) {
	for (const Hit& hit : hits) {
		if (hit.type != HitType::Setting || hit.setting != SettingId::FrameGenerationMultiplier) continue;
		const float progress = clamp01((x - hit.rect.left) / (hit.rect.right - hit.rect.left));
		const int value = std::clamp(1 + static_cast<int>(std::lround(progress * 3.0f)), 1, 4);
		if (pendingSettings.frameGenerationMultiplier != value) {
			pendingSettings.frameGenerationMultiplier = value;
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		return;
	}
}

void NativeGui::handleClick(float x, float y) {
	if (modalKind != ModalKind::None && modalTarget == 0.0f) return;
	if (modalKind != ModalKind::None && modalProgress < 0.92f) return;
	const bool updateModal = modalKind == ModalKind::Update;
	for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
		if (!contains(it->rect, x, y)) continue;
		if (pageProgress < 0.92f && it->type != HitType::Tab && !sourceModal && !profileModal && !captureModal && modalKind != ModalKind::Number && !updateModal) continue;
		if ((sourceModal || profileModal || captureModal || modalKind == ModalKind::Number || updateModal) &&
			it->type != HitType::CaptureCancel && it->type != HitType::CaptureUnbind &&
			it->type != HitType::NumberApply && it->type != HitType::SourceLegacy && it->type != HitType::SourceEnhanced &&
			it->type != HitType::Profile && it->type != HitType::UpdateCancel && it->type != HitType::UpdateInstall) continue;
		switch (it->type) {
		case HitType::Tab:
			if (activeTab != it->tab) {
				activeTab = it->tab;
				scrollOffset = 0.0f;
				targetScrollOffset = 0.0f;
				pageProgress = 0.0f;
				hoverHit = -1;
				hoverProgress = 0.0f;
			}
			break;
		case HitType::Setting:
			if (settingIsBoolean(it->setting)) settingBool(pendingSettings, it->setting) = !settingBool(pendingSettings, it->setting);
			else if (it->setting == SettingId::FrameGenerationMultiplier) {
				frameGenerationSliderDragging = true;
				SetCapture(hwnd);
				updateFrameGenerationSlider(x);
			}
			else if (settingIsInteger(it->setting)) beginNumberCapture(it->setting);
			else beginCapture(it->setting);
			break;
		case HitType::Save: save(); break;
		case HitType::Discard: discard(); break;
		case HitType::Import: sourceModal = true; clearMessage(); openModal(ModalKind::Source); break;
		case HitType::MatchGtaKeys:
			pendingSettings.rpgTabSwitchHotkey = pendingSettings.rpgKey;
			pendingSettings.stickyBombTabSwitchHotkey = pendingSettings.stickyBombKey;
			pendingSettings.sniperTabSwitchHotkey = pendingSettings.sniperKey;
			pendingSettings.pistolTabSwitchHotkey = pendingSettings.pistolKey;
			pendingSettings.shotgunTabSwitchHotkey = pendingSettings.shotgunKey;
			pendingSettings.rifleTabSwitchHotkey = pendingSettings.rifleKey;
			pendingSettings.smgTabSwitchHotkey = pendingSettings.smgKey;
			pendingSettings.fistsTabSwitchHotkey = pendingSettings.fistsKey;
			pendingSettings.meleeTabSwitchHotkey = pendingSettings.meleeKey;
			setMessage("Tab switch keys now match the GTA weapon keys.");
			break;
		case HitType::Hide: hide(); break;
		case HitType::CaptureCancel:
			if (modalKind == ModalKind::Number) finishNumberCapture(false);
			else closeModal();
			break;
		case HitType::CaptureUnbind: settingKey(pendingSettings, captureSetting) = {}; closeModal(); break;
		case HitType::NumberApply: finishNumberCapture(true); break;
		case HitType::SourceLegacy: importSource(false); break;
		case HitType::SourceEnhanced: importSource(true); break;
		case HitType::Profile: importProfile(it->index); break;
		case HitType::UpdateCancel:
			if (!updateInstalling) closeModal();
			break;
		case HitType::UpdateInstall:
			if (!updateInstalling) {
				updateInstalling = true;
				updateStatus = "Downloading the latest build...";
				if (updateInstallCallback) updateInstallCallback(updateInfo);
				else {
					updateInstalling = false;
					updateStatus = "The updater is unavailable.";
				}
			}
			break;
		}
		InvalidateRect(hwnd, nullptr, FALSE);
		return;
	}
}

void NativeGui::beginCapture(SettingId id) {
	{
		std::lock_guard lock(captureMutex);
		captureActive = true;
		captureSetting = id;
		captureModifiers.clear();
		captureSawModifier = false;
		captureResultReady = false;
		capturedChord = {};
	}
	captureModal = true;
	openModal(ModalKind::Capture);
	setMessage("Press Escape to cancel capture.");
}

void NativeGui::beginNumberCapture(SettingId id) {
	{
		std::lock_guard lock(captureMutex);
		numberCaptureActive = true;
		captureSetting = id;
	}
	SetWindowTextW(numberEdit, std::to_wstring(settingInt(pendingSettings, id)).c_str());
	ShowWindow(numberEdit, SW_SHOWNOACTIVATE);
	SetFocus(numberEdit);
	SendMessageW(numberEdit, EM_SETSEL, 0, -1);
	openModal(ModalKind::Number);
	setMessage("Enter a number, then press Enter or Apply.");
}

void NativeGui::closeModal() {
	{
		std::lock_guard lock(captureMutex);
		captureActive = false;
		numberCaptureActive = false;
		captureResultReady = false;
		captureModifiers.clear();
	}
	ShowWindow(numberEdit, SW_HIDE);
	captureModal = false;
	sourceModal = false;
	profileModal = false;
	modalTarget = 0.0f;
	clearMessage();
}

void NativeGui::save() {
	if (sameSettings(savedSettings, pendingSettings)) return;
	std::string error;
	if (!applyCallback || !applyCallback(pendingSettings, error)) {
		setError(error.empty() ? "Could not apply settings." : error);
		return;
	}
	savedSettings = pendingSettings;
	setMessage("Settings saved.");
}

void NativeGui::discard() {
	pendingSettings = savedSettings;
	setMessage("Changes discarded.");
}

void NativeGui::importSource(bool enhanced) {
	importEnhanced = enhanced;
	std::string error;
	profiles = GtaProfileImporter::findProfiles(enhanced, error);
	if (profiles.empty()) {
		setMessage(error.empty() ? "No profiles found." : error, true);
		profileModal = true;
		sourceModal = false;
		openModal(ModalKind::Profile);
		return;
	}
	profileModal = true;
	sourceModal = false;
	clearMessage();
	openModal(ModalKind::Profile);
}

void NativeGui::importProfile(size_t index) {
	if (index >= profiles.size()) return;
	std::vector<std::string> warnings;
	std::string error;
	if (!GtaProfileImporter::importProfile(profiles[index], pendingSettings, warnings, error)) {
		setError(error.empty() ? "Could not import that profile." : error);
		return;
	}
	profileModal = false;
	modalTarget = 0.0f;
	std::string imported = "Imported " + toUtf8(profiles[index].name);
	if (!warnings.empty()) imported += " (" + std::to_string(warnings.size()) + " mappings were not present)";
	setMessage(std::move(imported));
}

void NativeGui::setMessage(std::string value, bool error) {
	message = std::move(value);
	messageError = error;
	if (!hwnd || message.empty()) return;
	SetTimer(hwnd, kMessageTimer, 3000, nullptr);
}

void NativeGui::clearMessage() {
	if (hwnd) KillTimer(hwnd, kMessageTimer);
	message.clear();
	messageError = false;
}

void NativeGui::setError(std::string error) {
	setMessage(std::move(error), true);
}

bool NativeGui::isModifier(DWORD keyCode) {
	return keyCode == VK_CONTROL || keyCode == VK_LCONTROL || keyCode == VK_RCONTROL ||
		keyCode == VK_SHIFT || keyCode == VK_LSHIFT || keyCode == VK_RSHIFT ||
		keyCode == VK_MENU || keyCode == VK_LMENU || keyCode == VK_RMENU ||
		keyCode == VK_LWIN || keyCode == VK_RWIN;
}

void NativeGui::completeCaptureLocked(const KeyChord& chord) {
	capturedChord = chord;
	captureResultReady = true;
	captureActive = false;
	captureModifiers.clear();
	if (hwnd) PostMessageW(hwnd, WM_APP_CAPTURE_RESULT, 0, 0);
}

bool NativeGui::captureKeyboard(DWORD keyCode, bool down) {
	std::lock_guard lock(captureMutex);
	if (numberCaptureActive) return false;
	if (!captureActive) return false;
	if (down) {
		if (keyCode == VK_ESCAPE && captureModifiers.empty()) {
			captureActive = false;
			captureResultReady = false;
			PostMessageW(hwnd, WM_APP_CAPTURE_RESULT, 0, 0);
			return true;
		}
		if (isModifier(keyCode)) {
			if (std::find(captureModifiers.begin(), captureModifiers.end(), static_cast<WORD>(keyCode)) == captureModifiers.end()) captureModifiers.push_back(static_cast<WORD>(keyCode));
			captureSawModifier = true;
			return true;
		}
		KeyChord chord;
		chord.key = static_cast<WORD>(keyCode);
		chord.modifiers = captureModifiers;
		completeCaptureLocked(chord);
		return true;
	}
	if (!isModifier(keyCode)) return true;
	auto it = std::find(captureModifiers.begin(), captureModifiers.end(), static_cast<WORD>(keyCode));
	if (it != captureModifiers.end()) captureModifiers.erase(it);
	if (captureModifiers.empty() && captureSawModifier) {
		completeCaptureLocked({static_cast<WORD>(keyCode), {}});
	}
	return true;
}

bool NativeGui::captureMouse(WPARAM message, WORD keyCode) {
	if (message != WM_MOUSEWHEEL && message != WM_MOUSEHWHEEL &&
		message != WM_LBUTTONDOWN && message != WM_RBUTTONDOWN && message != WM_MBUTTONDOWN &&
		message != WM_XBUTTONDOWN) return false;
	std::lock_guard lock(captureMutex);
	if (!captureActive) return false;
	POINT cursor{};
	GetCursorPos(&cursor);
	ScreenToClient(hwnd, &cursor);
	const float cursorX = pixelsToDips(hwnd, cursor.x);
	const float cursorY = pixelsToDips(hwnd, cursor.y);
	for (const Hit& hit : hits) {
		if ((hit.type == HitType::CaptureCancel || hit.type == HitType::CaptureUnbind) && contains(hit.rect, cursorX, cursorY)) {
			return false;
		}
	}
	KeyChord chord{keyCode, captureModifiers};
	if (GetAsyncKeyState(VK_CONTROL) & 0x8000) chord.modifiers.push_back(VK_CONTROL);
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000) chord.modifiers.push_back(VK_SHIFT);
	if (GetAsyncKeyState(VK_MENU) & 0x8000) chord.modifiers.push_back(VK_MENU);
	completeCaptureLocked(chord);
	return true;
}

bool NativeGui::captureKeyboardEvent(DWORD keyCode, bool down) {
	return getInstance().captureKeyboard(keyCode, down);
}

bool NativeGui::captureMouseEvent(WPARAM message, WORD keyCode) {
	return getInstance().captureMouse(message, keyCode);
}

void NativeGui::finishCapture() {
	KeyChord chord;
	bool result = false;
	{
		std::lock_guard lock(captureMutex);
		result = captureResultReady;
		chord = capturedChord;
		captureResultReady = false;
		captureActive = false;
	}
	if (result && !chord.empty()) {
		settingKey(pendingSettings, captureSetting) = chord;
		setMessage("Captured " + chord.serialize());
	} else {
		setMessage("Hotkey capture cancelled.");
	}
	captureModal = false;
	modalTarget = 0.0f;
}

void NativeGui::finishNumberCapture(bool accept) {
	{
		std::lock_guard lock(captureMutex);
		if (!numberCaptureActive) return;
		if (!accept) numberCaptureActive = false;
	}
	if (!accept) {
		ShowWindow(numberEdit, SW_HIDE);
		modalTarget = 0.0f;
		setMessage("Number entry cancelled.");
		return;
	}

	const int length = numberEdit ? GetWindowTextLengthW(numberEdit) : 0;
	std::wstring value(static_cast<size_t>(length) + 1, L'\0');
	if (numberEdit) GetWindowTextW(numberEdit, value.data(), length + 1);
	value.resize(std::wcslen(value.c_str()));
	try {
		size_t consumed = 0;
		const int number = std::stoi(value, &consumed);
		if (value.empty() || consumed != value.size()) throw std::invalid_argument("not a whole number");
		const int clamped = std::clamp(number, 1, 360);
		settingInt(pendingSettings, captureSetting) = clamped;
		SetWindowTextW(numberEdit, std::to_wstring(clamped).c_str());
		{
			std::lock_guard lock(captureMutex);
			numberCaptureActive = false;
		}
		ShowWindow(numberEdit, SW_HIDE);
		modalTarget = 0.0f;
		setMessage("Turn amount set to " + std::to_string(clamped) + " degrees.");
	} catch (...) {
		setMessage("Enter a whole number between 1 and 360.", true);
		SetFocus(numberEdit);
		SendMessageW(numberEdit, EM_SETSEL, 0, -1);
	}
}

LRESULT CALLBACK NativeGui::numberEditProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam) {
	auto* gui = reinterpret_cast<NativeGui*>(GetWindowLongPtrW(edit, GWLP_USERDATA));
	if (gui && message == WM_KEYDOWN) {
		if (wParam == VK_RETURN) {
			PostMessageW(gui->hwnd, WM_APP_NUMBER_APPLY, 0, 0);
			return 0;
		}
		if (wParam == VK_ESCAPE) {
			PostMessageW(gui->hwnd, WM_APP_NUMBER_CANCEL, 0, 0);
			return 0;
		}
	}
	if (gui && gui->numberEditDefaultProc)
		return CallWindowProcW(gui->numberEditDefaultProc, edit, message, wParam, lParam);
	return DefWindowProcW(edit, message, wParam, lParam);
}

LRESULT CALLBACK NativeGui::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	NativeGui* gui = reinterpret_cast<NativeGui*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) {
		auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		gui = static_cast<NativeGui*>(create->lpCreateParams);
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(gui));
		gui->hwnd = window;
	}
	return gui ? gui->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT NativeGui::handleMessage(UINT messageId, WPARAM wParam, LPARAM lParam) {
	switch (messageId) {
	case WM_PAINT: {
		PAINTSTRUCT paintStruct{};
		BeginPaint(hwnd, &paintStruct);
		paint();
		EndPaint(hwnd, &paintStruct);
		return 0;
	}
	case WM_CTLCOLOREDIT:
		if (reinterpret_cast<HWND>(lParam) == numberEdit) {
			HDC dc = reinterpret_cast<HDC>(wParam);
			SetTextColor(dc, RGB(66, 209, 237));
			SetBkColor(dc, RGB(10, 15, 31));
			SetBkMode(dc, TRANSPARENT);
			return reinterpret_cast<LRESULT>(numberEditBrush);
		}
		break;
	case WM_ERASEBKGND: return 1;
	case WM_GETMINMAXINFO: {
		auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
		UINT dpi = windowDpi(hwnd);
		limits->ptMinTrackSize.x = scaleDipsToPixels(1120, dpi);
		limits->ptMinTrackSize.y = scaleDipsToPixels(600, dpi);
		return 0;
	}
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED) {
			hide();
			return 0;
		}
		if (renderTarget) renderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_MOUSEMOVE:
		handleMouseMove(pixelsToDips(hwnd, GET_X_LPARAM(lParam)), pixelsToDips(hwnd, GET_Y_LPARAM(lParam)));
		return 0;
	case WM_MOUSEWHEEL:
		if (!sourceModal && !profileModal && !captureModal && modalKind != ModalKind::Number && modalKind != ModalKind::Update) {
			int delta = GET_WHEEL_DELTA_WPARAM(wParam);
			targetScrollOffset = std::max(0.0f, targetScrollOffset - static_cast<float>(delta) / 4.0f);
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		return 0;
	case WM_LBUTTONDOWN:
		SetFocus(hwnd);
		handleClick(pixelsToDips(hwnd, GET_X_LPARAM(lParam)), pixelsToDips(hwnd, GET_Y_LPARAM(lParam)));
		return 0;
	case WM_LBUTTONUP:
		if (frameGenerationSliderDragging) {
			frameGenerationSliderDragging = false;
			ReleaseCapture();
		}
		return 0;
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE && (sourceModal || profileModal || captureModal || modalKind == ModalKind::Number ||
			(modalKind == ModalKind::Update && !updateInstalling))) closeModal();
		else if (wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) save();
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_TIMER:
		if (wParam == kMessageTimer) {
			clearMessage();
			InvalidateRect(hwnd, nullptr, FALSE);
		} else if (wParam == kAnimationTimer) advanceAnimations();
		return 0;
	case WM_APP_STATUS: {
		std::lock_guard lock(asyncMutex);
		rtssStatus = pendingRtssStatus;
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	}
	case WM_APP_CAPTURE_RESULT:
		finishCapture();
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_APP_NUMBER_APPLY:
		finishNumberCapture(true);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_APP_NUMBER_CANCEL:
		finishNumberCapture(false);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_APP_UPDATE_CHECK: {
		Updater::UpdateInfo info;
		{
			std::lock_guard lock(asyncMutex);
			info = pendingUpdateInfo;
		}
		if (info.ok && info.available) {
			updateInfo = std::move(info);
			updateInstalling = false;
			updateStatus = "Your saved settings and customizations will stay in place.";
			openModal(ModalKind::Update);
		}
		return 0;
	}
	case WM_APP_UPDATE_RESULT: {
		Updater::InstallResult result;
		{
			std::lock_guard lock(asyncMutex);
			result = std::move(pendingUpdateResult);
		}
		if (result.ok) {
			updateInstalling = false;
			exit();
		} else {
			updateInstalling = false;
			updateStatus = result.error.empty() ? "Update failed. Try again." : result.error;
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		return 0;
	}
	case WM_APP_TRAY:
		if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) show();
		else if (lParam == WM_RBUTTONUP) {
			POINT point{};
			GetCursorPos(&point);
			HMENU menu = CreatePopupMenu();
			AppendMenuW(menu, MF_STRING, 1, L"Show settings");
			AppendMenuW(menu, MF_STRING, 2, L"Exit");
			SetForegroundWindow(hwnd);
			int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, hwnd, nullptr);
			DestroyMenu(menu);
			if (command == 1) show();
			if (command == 2) exit();
		}
		return 0;
	case WM_COMMAND:
		if (reinterpret_cast<HWND>(lParam) == numberEdit && HIWORD(wParam) == EN_CHANGE) {
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		}
		if (LOWORD(wParam) == 2) exit();
		return 0;
	case WM_DPICHANGED: {
		UINT dpi = HIWORD(wParam);
		if (renderTarget && dpi) renderTarget->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
		auto* suggested = reinterpret_cast<RECT*>(lParam);
		SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
		updateNumberEditFont();
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	}
	case WM_CLOSE:
		if (!updateInstalling) exit();
		return 0;
	case WM_DESTROY:
		if (numberEditFont) {
			DeleteObject(numberEditFont);
			numberEditFont = nullptr;
		}
		if (numberEditBrush) {
			DeleteObject(numberEditBrush);
			numberEditBrush = nullptr;
		}
		if (trayAdded) {
			Shell_NotifyIconW(NIM_DELETE, &tray);
			trayAdded = false;
		}
		if (allowDestroy) PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, messageId, wParam, lParam);
}
