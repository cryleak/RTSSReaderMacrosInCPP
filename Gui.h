#pragma once

#include "RTSSReader.h"
#include "Settings.h"
#include "Updater.h"

#include <d2d1.h>
#include <dwrite.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

class NativeGui {
public:
	using ApplyCallback = std::function<bool(const MacroSettings&, std::string&)>;
	using UpdateInstallCallback = std::function<void(const Updater::UpdateInfo&)>;
	struct Rect { float left, top, right, bottom; };
	enum class Tab { General, Weapons, Controls, Advanced };

	static NativeGui& getInstance();

	bool create(HINSTANCE instance, const MacroSettings& settings, ApplyCallback apply, UpdateInstallCallback updateInstall);
	void show();
	void hide();
	void exit();
	HWND window() const { return hwnd; }

	void postRtssStatus(const RtssStatus& status);
	void postUpdateCheck(const Updater::UpdateInfo& info);
	void postUpdateInstallResult(const Updater::InstallResult& result);

	static bool captureKeyboardEvent(DWORD keyCode, bool down);
	static bool captureMouseEvent(WPARAM message, WORD keyCode);

private:
	NativeGui() = default;
	~NativeGui() = default;
	NativeGui(const NativeGui&) = delete;
	void operator=(const NativeGui&) = delete;

	enum class HitType { Tab, Setting, Save, Discard, Import, MatchGtaKeys, Hide, CaptureCancel, CaptureUnbind, NumberApply, Profile, SourceLegacy, SourceEnhanced, UpdateCancel, UpdateInstall };
	enum class ModalKind { None, Source, Profile, Capture, Number, Update };
	struct Hit { Rect rect{}; HitType type{}; SettingId setting{}; size_t index = 0; Tab tab{}; };
	struct SettingRow { SettingId id; Tab tab; const wchar_t* label; const wchar_t* description; };

	static constexpr UINT WM_APP_STATUS = WM_APP + 10;
	static constexpr UINT WM_APP_CAPTURE_RESULT = WM_APP + 11;
	static constexpr UINT WM_APP_TRAY = WM_APP + 12;
	static constexpr UINT WM_APP_NUMBER_APPLY = WM_APP + 13;
	static constexpr UINT WM_APP_NUMBER_CANCEL = WM_APP + 14;
	static constexpr UINT WM_APP_UPDATE_CHECK = WM_APP + 15;
	static constexpr UINT WM_APP_UPDATE_RESULT = WM_APP + 16;
	static constexpr UINT_PTR kMessageTimer = 1;
	static constexpr UINT_PTR kAnimationTimer = 2;

	static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK numberEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

	bool createDeviceResources();
	void discardDeviceResources();
	void updateNumberEditFont();
	void paint();
	void advanceAnimations();
	void openModal(ModalKind kind);
	void drawSidebar(float height);
	void drawPage(float width, float height);
	void drawModal(float width, float height);
	void drawProfileModal(float width, float height);
	void drawCaptureModal(float width, float height);
	void drawNumberModal(float width, float height);
	void drawUpdateModal(float width, float height);
	void drawText(const std::wstring& text, Rect rect, IDWriteTextFormat* format, D2D1::ColorF color);
	void fill(Rect rect, D2D1::ColorF color, float radius = 0.0f);
	void fillGradient(Rect rect, D2D1::ColorF first, D2D1::ColorF second, float radius = 0.0f);
	void stroke(Rect rect, D2D1::ColorF color, float radius = 0.0f, float width = 1.0f);
	void addHit(Rect rect, HitType type, SettingId setting = SettingId::BstHotkey, size_t index = 0, Tab tab = Tab::General);
	void handleClick(float x, float y);
	void handleMouseMove(float x, float y);
	void updateFrameGenerationSlider(float x);
	void beginCapture(SettingId id);
	void beginNumberCapture(SettingId id);
	void finishCapture();
	void finishNumberCapture(bool accept);
	void closeModal();
	void save();
	void discard();
	void importSource(bool enhanced);
	void importProfile(size_t index);
	void setMessage(std::string message, bool error = false);
	void clearMessage();
	void setError(std::string error);
	void updateWindowTitle();

	bool captureKeyboard(DWORD keyCode, bool down);
	bool captureMouse(WPARAM message, WORD keyCode);
	void completeCaptureLocked(const KeyChord& chord);
	static bool isModifier(DWORD keyCode);

	HINSTANCE instance = nullptr;
	HWND hwnd = nullptr;
	NOTIFYICONDATAW tray{};
	bool trayAdded = false;
	bool allowDestroy = false;
	ApplyCallback applyCallback;
	UpdateInstallCallback updateInstallCallback;

	MacroSettings savedSettings;
	MacroSettings pendingSettings;
	RtssStatus rtssStatus;
	Tab activeTab = Tab::General;
	float scrollOffset = 0.0f;
	float targetScrollOffset = 0.0f;
	float pageProgress = 1.0f;
	float hoverProgress = 0.0f;
	float modalProgress = 0.0f;
	float modalTarget = 0.0f;
	float capturePreviewProgress = 0.0f;
	float drawOpacity = 1.0f;
	ULONGLONG lastAnimationTick = 0;
	ModalKind modalKind = ModalKind::None;
	std::vector<Hit> hits;
	int hoverHit = -1;
	bool sourceModal = false;
	bool profileModal = false;
	bool captureModal = false;
	bool frameGenerationSliderDragging = false;
	bool importEnhanced = false;
	std::vector<GtaProfile> profiles;
	std::unordered_map<int, float> booleanProgress;
	std::string message;
	bool messageError = false;

	std::mutex asyncMutex;
	RtssStatus pendingRtssStatus;
	Updater::UpdateInfo pendingUpdateInfo;
	Updater::InstallResult pendingUpdateResult;
	Updater::UpdateInfo updateInfo;
	std::string updateStatus;
	bool updateInstalling = false;

	std::mutex captureMutex;
	bool captureActive = false;
	bool numberCaptureActive = false;
	SettingId captureSetting = SettingId::BstHotkey;
	std::vector<WORD> captureModifiers;
	bool captureSawModifier = false;
	bool captureResultReady = false;
	KeyChord capturedChord;
	HWND numberEdit = nullptr;
	WNDPROC numberEditDefaultProc = nullptr;
	HBRUSH numberEditBrush = nullptr;
	HFONT numberEditFont = nullptr;

	Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
	Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget;
	Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solidBrush;
	Microsoft::WRL::ComPtr<ID2D1Bitmap> ditherBitmap;
	Microsoft::WRL::ComPtr<ID2D1BitmapBrush> ditherBrush;
	std::unordered_map<std::uint64_t, Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush>> gradientBrushes;
	Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory;
	Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
	Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFormat;
	Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat;
	Microsoft::WRL::ComPtr<IDWriteTextFormat> navFormat;
};
