#include "keymap.h"
#include <winuser.h>
#include <algorithm>
#include <cctype>

key_to_vk_type g_key_to_vk[] = {
    {"numpad0", VK_NUMPAD0},
    {"numpad1", VK_NUMPAD1},
    {"numpad2", VK_NUMPAD2},
    {"numpad3", VK_NUMPAD3},
    {"numpad4", VK_NUMPAD4},
    {"numpad5", VK_NUMPAD5},
    {"numpad6", VK_NUMPAD6},
    {"numpad7", VK_NUMPAD7},
    {"numpad8", VK_NUMPAD8},
    {"numpad9", VK_NUMPAD9},
    {"numpadmult", VK_MULTIPLY},
    {"numpaddiv", VK_DIVIDE},
    {"numpadadd", VK_ADD},
    {"numpadsub", VK_SUBTRACT},
    {"numpaddot", VK_DECIMAL},
    {"numlock", VK_NUMLOCK},
    {"scrolllock", VK_SCROLL},
    {"capslock", VK_CAPITAL},
    {"escape", VK_ESCAPE},
    {"esc", VK_ESCAPE},
    {"tab", VK_TAB},
    {"space", VK_SPACE},
    {"backspace", VK_BACK},
    {"bs", VK_BACK},
    {"enter", VK_RETURN},
    {"return", VK_RETURN},
    {"numpaddel", VK_DELETE},
    {"numpadins", VK_INSERT},
    {"numpadclear", VK_CLEAR},
    {"numpadup", VK_UP},
    {"numpaddown", VK_DOWN},
    {"numpadleft", VK_LEFT},
    {"numpadright", VK_RIGHT},
    {"numpadhome", VK_HOME},
    {"numpadend", VK_END},
    {"numpadpgup", VK_PRIOR},
    {"numpadpgdn", VK_NEXT},
    {"printscreen", VK_SNAPSHOT},
    {"ctrlbreak", VK_CANCEL},
    {"pause", VK_PAUSE},
    {"break", VK_PAUSE},
    {"help", VK_HELP},
    {"sleep", VK_SLEEP},
    {"appskey", VK_APPS},
    {"lcontrol", VK_LCONTROL},
    {"rcontrol", VK_RCONTROL},
    {"lctrl", VK_LCONTROL},
    {"rctrl", VK_RCONTROL},
    {"lshift", VK_LSHIFT},
    {"rshift", VK_RSHIFT},
    {"lalt", VK_LMENU},
    {"ralt", VK_RMENU},
    {"lwin", VK_LWIN},
    {"rwin", VK_RWIN},
    {"control", VK_CONTROL},
    {"ctrl", VK_CONTROL},
    {"alt", VK_MENU},
    {"shift", VK_SHIFT},
    {"f1", VK_F1},
    {"f2", VK_F2},
    {"f3", VK_F3},
    {"f4", VK_F4},
    {"f5", VK_F5},
    {"f6", VK_F6},
    {"f7", VK_F7},
    {"f8", VK_F8},
    {"f9", VK_F9},
    {"f10", VK_F10},
    {"f11", VK_F11},
    {"f12", VK_F12},
    {"f13", VK_F13},
    {"f14", VK_F14},
    {"f15", VK_F15},
    {"f16", VK_F16},
    {"f17", VK_F17},
    {"f18", VK_F18},
    {"f19", VK_F19},
    {"f20", VK_F20},
    {"f21", VK_F21},
    {"f22", VK_F22},
    {"f23", VK_F23},
    {"f24", VK_F24},
    {"lbutton", VK_LBUTTON},
    {"rbutton", VK_RBUTTON},
    {"mbutton", VK_MBUTTON},
    {"xbutton1", VK_XBUTTON1},
    {"xbutton2", VK_XBUTTON2},
    {"wheeldown", 0x1000},
    {"wheelup", 0x1001},
    {"wheelleft", 0x1002},
    {"wheelright", 0x1003},
    {"browser_back", VK_BROWSER_BACK},
    {"browser_forward", VK_BROWSER_FORWARD},
    {"browser_refresh", VK_BROWSER_REFRESH},
    {"browser_stop", VK_BROWSER_STOP},
    {"browser_search", VK_BROWSER_SEARCH},
    {"browser_favorites", VK_BROWSER_FAVORITES},
    {"browser_home", VK_BROWSER_HOME},
    {"volume_mute", VK_VOLUME_MUTE},
    {"volume_down", VK_VOLUME_DOWN},
    {"volume_up", VK_VOLUME_UP},
    {"media_next", VK_MEDIA_NEXT_TRACK},
    {"media_prev", VK_MEDIA_PREV_TRACK},
    {"media_stop", VK_MEDIA_STOP},
    {"media_play_pause", VK_MEDIA_PLAY_PAUSE},
    {"launch_mail", VK_LAUNCH_MAIL},
    {"launch_media", VK_LAUNCH_MEDIA_SELECT},
    {"launch_app1", VK_LAUNCH_APP1},
    {"launch_app2", VK_LAUNCH_APP2},
    {"up", VK_UP},
    {"left", VK_LEFT},
    {"down", VK_DOWN},
    {"right", VK_RIGHT}
};

const size_t g_key_to_vk_size = sizeof(g_key_to_vk) / sizeof(g_key_to_vk[0]);

namespace
{
    std::string lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    std::wstring localizedKeyName(WORD vkCode)
    {
        HKL layout = GetKeyboardLayout(0);
        UINT mappedChar = MapVirtualKeyExW(vkCode, MAPVK_VK_TO_CHAR, layout) & 0x7FFFFFFF;
        if (mappedChar >= 0x21 && mappedChar <= 0xFFFF) return std::wstring(1, static_cast<wchar_t>(mappedChar));

        UINT scanCode = MapVirtualKeyExW(vkCode, MAPVK_VK_TO_VSC_EX, layout);
        if (!scanCode) return {};
        LONG keyNameParam = static_cast<LONG>((scanCode & 0xFF) << 16);
        if ((scanCode & 0xFF00) == 0xE000 || (scanCode & 0xFF00) == 0xE100) keyNameParam |= 1L << 24;
        wchar_t name[64]{};
        int length = GetKeyNameTextW(keyNameParam, name, std::size(name));
        return length > 0 ? std::wstring(name, length) : std::wstring{};
    }

    std::optional<WORD> unicodeKeyToVk(const std::string& value)
    {
        if (value.empty()) return std::nullopt;
        int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (length != 1) return std::nullopt;
        wchar_t character{};
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &character, 1);
        SHORT translated = VkKeyScanExW(character, GetKeyboardLayout(0));
        return translated == -1 ? std::nullopt : std::optional<WORD>(LOBYTE(translated));
    }
}

std::optional<WORD> keyNameToVk(const std::string& name)
{
    const std::string normalized = lower(name);
    if (normalized == "oem_1" || normalized == "semicolon") return VK_OEM_1;
    if (normalized == "oem_plus" || normalized == "plus") return VK_OEM_PLUS;
    if (normalized == "oem_comma" || normalized == "comma") return VK_OEM_COMMA;
    if (normalized == "oem_minus" || normalized == "minus") return VK_OEM_MINUS;
    if (normalized == "oem_period" || normalized == "period") return VK_OEM_PERIOD;
    if (normalized == "oem_2" || normalized == "slash") return VK_OEM_2;
    if (normalized == "oem_3" || normalized == "grave") return VK_OEM_3;
    if (normalized == "oem_4" || normalized == "lbracket") return VK_OEM_4;
    if (normalized == "oem_5" || normalized == "backslash") return VK_OEM_5;
    if (normalized == "oem_6" || normalized == "rbracket") return VK_OEM_6;
    if (normalized == "oem_7" || normalized == "apostrophe") return VK_OEM_7;
    if (normalized == "oem_102") return VK_OEM_102;
    for (size_t i = 0; i < g_key_to_vk_size; ++i)
    {
        if (g_key_to_vk[i].keyName == normalized) return static_cast<WORD>(g_key_to_vk[i].vkCode);
    }
    if (auto translated = unicodeKeyToVk(normalized)) return translated;
    return std::nullopt;
}

std::string keyName(WORD vkCode)
{
    switch (vkCode)
    {
    case VK_CONTROL: return "ctrl";
    case VK_LCONTROL: return "lctrl";
    case VK_RCONTROL: return "rctrl";
    case VK_SHIFT: return "shift";
    case VK_LSHIFT: return "lshift";
    case VK_RSHIFT: return "rshift";
    case VK_MENU: return "alt";
    case VK_LMENU: return "lalt";
    case VK_RMENU: return "ralt";
    case VK_LWIN: return "lwin";
    case VK_RWIN: return "rwin";
    case VK_OEM_1: return "oem_1";
    case VK_OEM_PLUS: return "oem_plus";
    case VK_OEM_COMMA: return "oem_comma";
    case VK_OEM_MINUS: return "oem_minus";
    case VK_OEM_PERIOD: return "oem_period";
    case VK_OEM_2: return "oem_2";
    case VK_OEM_3: return "oem_3";
    case VK_OEM_4: return "oem_4";
    case VK_OEM_5: return "oem_5";
    case VK_OEM_6: return "oem_6";
    case VK_OEM_7: return "oem_7";
    case VK_OEM_102: return "oem_102";
    case 0x1000: return "wheeldown";
    case 0x1001: return "wheelup";
    case 0x1002: return "wheelleft";
    case 0x1003: return "wheelright";
    default: break;
    }
    for (size_t i = 0; i < g_key_to_vk_size; ++i)
    {
        if (static_cast<WORD>(g_key_to_vk[i].vkCode) == vkCode) return g_key_to_vk[i].keyName;
    }
    if (vkCode >= 'A' && vkCode <= 'Z') return std::string(1, static_cast<char>(std::tolower(vkCode)));
    if (vkCode >= '0' && vkCode <= '9') return std::string(1, static_cast<char>(vkCode));
    char name[64]{};
    UINT scanCode = MapVirtualKeyA(vkCode, MAPVK_VK_TO_VSC);
    if (scanCode && GetKeyNameTextA(static_cast<LONG>(scanCode) << 16, name, sizeof(name)))
    {
        return lower(name);
    }
    return "vk_" + std::to_string(vkCode);
}

std::wstring displayKeyName(WORD vkCode)
{
    switch (vkCode)
    {
    case VK_CONTROL: return L"Ctrl";
    case VK_LCONTROL: return L"LCtrl";
    case VK_RCONTROL: return L"RCtrl";
    case VK_SHIFT: return L"Shift";
    case VK_LSHIFT: return L"LShift";
    case VK_RSHIFT: return L"RShift";
    case VK_MENU: return L"Alt";
    case VK_LMENU: return L"LAlt";
    case VK_RMENU: return L"RAlt";
    case VK_LWIN: return L"Win";
    case VK_RWIN: return L"RWin";
    case 0x1000: return L"Wheel Down";
    case 0x1001: return L"Wheel Up";
    case 0x1002: return L"Wheel Left";
    case 0x1003: return L"Wheel Right";
    default: break;
    }
    if (std::wstring localized = localizedKeyName(vkCode); !localized.empty()) return localized;
    std::string name = keyName(vkCode);
    if (name.size() == 1) return std::wstring(1, static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(name[0]))));
    std::wstring result(name.begin(), name.end());
    if (result.rfind(L"numpad", 0) == 0) result.replace(0, 6, L"Num ");
    if (result.rfind(L"f", 0) == 0 && result.size() > 1) result[0] = L'F';
    if (result.rfind(L"oem_", 0) == 0) result = L"OEM " + result.substr(4);
    return result;
}
