#include "Settings.h"

#include "keymap.h"

#include <Windows.h>
#include <ShlObj.h>
#include <msxml6.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <wrl/client.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "msxml6.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

    std::string trim(std::string value)
    {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    std::string lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    std::vector<std::string> split(const std::string& value, char delimiter)
    {
        std::vector<std::string> result;
        std::stringstream stream(value);
        std::string part;
        while (std::getline(stream, part, delimiter)) result.push_back(trim(part));
        return result;
    }

    std::wstring utf8ToWide(const std::string& value)
    {
        if (value.empty()) return {};
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
        UINT codePage = CP_UTF8;
        if (!length)
        {
            codePage = CP_ACP;
            length = MultiByteToWideChar(codePage, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        }
        std::wstring result(length, L'\0');
        MultiByteToWideChar(codePage, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
        return result;
    }

    std::string wideToUtf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
        return result;
    }

    std::string rageKeyToKeyName(std::string value)
    {
        value = lower(trim(value));
        static const std::unordered_map<std::string, std::string> aliases = {
            {"key_back", "backspace"}, {"key_tab", "tab"}, {"key_return", "enter"},
            {"key_pause", "pause"}, {"key_capital", "capslock"}, {"key_escape", "esc"},
            {"key_space", "space"}, {"key_pageup", "pgup"}, {"key_prior", "pgup"},
            {"key_pagedown", "pgdn"}, {"key_next", "pgdn"}, {"key_end", "end"},
            {"key_home", "home"}, {"key_left", "left"}, {"key_up", "up"},
            {"key_right", "right"}, {"key_down", "down"}, {"key_snapshot", "printscreen"},
            {"key_sysrq", "printscreen"}, {"key_insert", "insert"}, {"key_delete", "delete"},
            {"key_lwin", "lwin"}, {"key_rwin", "rwin"}, {"key_apps", "appskey"},
            {"key_numlock", "numlock"}, {"key_scroll", "scrolllock"},
            {"key_lshift", "lshift"}, {"key_rshift", "rshift"},
            {"key_lcontrol", "lctrl"}, {"key_rcontrol", "rctrl"},
            {"key_lmenu", "lalt"}, {"key_rmenu", "ralt"},
            {"key_multiply", "numpadmult"}, {"key_add", "numpadadd"},
            {"key_subtract", "numpadsub"}, {"key_decimal", "numpaddot"}, {"key_divide", "numpaddiv"},
            {"key_numpadenter", "enter"}, {"key_semicolon", "oem_1"}, {"key_plus", "oem_plus"},
            {"key_comma", "oem_comma"}, {"key_minus", "oem_minus"}, {"key_period", "oem_period"},
            {"key_slash", "oem_2"}, {"key_grave", "oem_3"}, {"key_lbracket", "oem_4"},
            {"key_backslash", "oem_5"}, {"key_rbracket", "oem_6"}, {"key_apostrophe", "oem_7"},
            {"mouse_left", "lbutton"}, {"mouse_right", "rbutton"}, {"mouse_middle", "mbutton"},
            {"mouse_extrabtn1", "xbutton1"}, {"mouse_extrabtn2", "xbutton2"},
            {"iom_wheel_up", "wheelup"}, {"iom_wheel_down", "wheeldown"},
        };
        if (auto it = aliases.find(value); it != aliases.end()) return it->second;
        if (value.rfind("key_", 0) == 0 && value.size() == 5 && value[4] >= '0' && value[4] <= '9') return value.substr(4);
        if (value.rfind("key_", 0) == 0 && value.size() == 5 && value[4] >= 'a' && value[4] <= 'z') return value.substr(4);
        if (value.rfind("key_f", 0) == 0) return value.substr(4);
        if (value.rfind("key_numpad", 0) == 0) return value.substr(4);
        return value;
    }

    std::wstring formatFileTime(const std::filesystem::path& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return L"Unknown time";
        SYSTEMTIME utc{}, local{};
        if (!FileTimeToSystemTime(&data.ftLastWriteTime, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return L"Unknown time";
        wchar_t buffer[64]{};
        GetDateFormatEx(nullptr, DATE_SHORTDATE, &local, nullptr, buffer, std::size(buffer), nullptr);
        std::wstring result = buffer;
        result += L" ";
        GetTimeFormatEx(nullptr, TIME_NOSECONDS, &local, nullptr, buffer, std::size(buffer));
        result += buffer;
        return result;
    }

    bool loadXmlText(const std::filesystem::path& path, ComPtr<IXMLDOMDocument2>& document, std::string& error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "Could not read " + path.string();
            return false;
        }
        std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::wstring xml = utf8ToWide(bytes);
        if (xml.empty())
        {
            error = "The selected control file is empty or not valid text.";
            return false;
        }

        HRESULT hr = CoCreateInstance(CLSID_DOMDocument60, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&document));
        if (FAILED(hr))
        {
            error = "MSXML 6 is unavailable.";
            return false;
        }
        document->put_async(VARIANT_FALSE);
        document->put_validateOnParse(VARIANT_FALSE);
        document->put_resolveExternals(VARIANT_FALSE);
        VARIANT_BOOL loaded = VARIANT_FALSE;
        BSTR xmlText = SysAllocString(xml.c_str());
        hr = document->loadXML(xmlText, &loaded);
        SysFreeString(xmlText);
        if (FAILED(hr) || loaded == VARIANT_FALSE)
        {
            error = "The selected control file is not valid XML.";
            return false;
        }
        return true;
    }

    std::optional<std::string> xmlValue(IXMLDOMDocument2* document, const std::string& input)
    {
        std::wstring xpath = L"//Item[normalize-space(Input)='" + utf8ToWide(input) + L"']/Parameters/Item[1]";
        BSTR query = SysAllocString(xpath.c_str());
        ComPtr<IXMLDOMNode> node;
        HRESULT hr = document->selectSingleNode(query, &node);
        SysFreeString(query);
        if (FAILED(hr) || !node) return std::nullopt;
        BSTR text = nullptr;
        if (FAILED(node->get_text(&text)) || !text) return std::nullopt;
        std::string result = wideToUtf8(text);
        SysFreeString(text);
        return result;
    }

    void importXmlKey(IXMLDOMDocument2* document, const char* xmlName, KeyChord& destination,
                      std::vector<std::string>& warnings, const char* fallbackName = nullptr, const char* defaultValue = nullptr)
    {
        auto value = xmlValue(document, xmlName);
        if (!value && fallbackName) value = xmlValue(document, fallbackName);
        if (!value)
        {
            if (defaultValue)
            {
                auto defaultKey = KeyChord::parse(defaultValue);
                if (defaultKey)
                {
                    destination = *defaultKey;
                    return;
                }
            }
            warnings.emplace_back(std::string("Missing ") + xmlName);
            return;
        }
        auto key = KeyChord::parse(rageKeyToKeyName(*value));
        if (!key)
        {
            warnings.emplace_back(std::string("Unsupported key in ") + xmlName + ": " + *value);
            return;
        }
        destination = *key;
    }
}

bool KeyChord::operator==(const KeyChord& other) const
{
    if (key != other.key || modifiers.size() != other.modifiers.size()) return false;
    std::vector<WORD> left = modifiers;
    std::vector<WORD> right = other.modifiers;
    std::sort(left.begin(), left.end());
    std::sort(right.begin(), right.end());
    return left == right;
}

std::string KeyChord::serialize() const
{
    if (empty()) return {};
    std::string result;
    std::vector<WORD> ordered = modifiers;
    std::sort(ordered.begin(), ordered.end());
    for (WORD modifier : ordered)
    {
        if (!result.empty()) result += "+";
        result += keyName(modifier);
    }
    if (!result.empty()) result += "+";
    result += keyName(key);
    return result;
}

std::wstring KeyChord::displayName() const
{
    if (empty()) return L"Not bound";
    std::wstring result;
    std::vector<WORD> ordered = modifiers;
    std::sort(ordered.begin(), ordered.end());
    for (WORD modifier : ordered)
    {
        if (!result.empty()) result += L" + ";
        result += displayKeyName(modifier);
    }
    if (!result.empty()) result += L" + ";
    result += displayKeyName(key);
    return result;
}

std::optional<KeyChord> KeyChord::parse(const std::string& value)
{
    std::string input = trim(value);
    if (input.empty()) return KeyChord{};
    KeyChord result;
    for (std::string part : split(input, '+'))
    {
        if (part.empty()) continue;
        std::string normalized = lower(part);
        bool isModifier = normalized == "ctrl" || normalized == "control" || normalized == "lctrl" || normalized == "rctrl" ||
            normalized == "shift" || normalized == "lshift" || normalized == "rshift" || normalized == "alt" ||
            normalized == "lalt" || normalized == "ralt" || normalized == "win" || normalized == "lwin" || normalized == "rwin";
        auto code = keyNameToVk(normalized);
        if (!code) return std::nullopt;
        if (isModifier && result.key == 0) result.modifiers.push_back(*code);
        else if (result.key == 0) result.key = *code;
        else return std::nullopt;
    }
    if (result.key == 0 && !result.modifiers.empty())
    {
        result.key = result.modifiers.back();
        result.modifiers.pop_back();
    }
    std::sort(result.modifiers.begin(), result.modifiers.end());
    result.modifiers.erase(std::unique(result.modifiers.begin(), result.modifiers.end()), result.modifiers.end());
    return result;
}

MacroSettings defaultSettings()
{
    return {};
}

const std::vector<SettingDefinition>& settingDefinitions()
{
    static const std::vector definitions = {
        SettingDefinition{SettingId::BstHotkey, "bst_hotkey", SettingType::KeyChord, &MacroSettings::bstHotkey},
        SettingDefinition{SettingId::ThermalHotkey, "thermal_hotkey", SettingType::KeyChord, &MacroSettings::thermalHotkey},
        SettingDefinition{SettingId::ThermalNightVision, "thermal_night_vision", SettingType::Boolean, nullptr, &MacroSettings::thermalNightVision},
        SettingDefinition{SettingId::SnacksHotkey, "snacks_hotkey", SettingType::KeyChord, &MacroSettings::snacksHotkey},
        SettingDefinition{SettingId::AmmoHotkey, "ammo_hotkey", SettingType::KeyChord, &MacroSettings::ammoHotkey},
        SettingDefinition{SettingId::QuickTurnHotkey, "quick_turn_hotkey", SettingType::KeyChord, &MacroSettings::quickTurnHotkey},
        SettingDefinition{SettingId::QuickTurnDegrees, "quick_turn_degrees", SettingType::Integer, nullptr, nullptr, &MacroSettings::quickTurnDegrees},
        SettingDefinition{SettingId::FrameGenerationMultiplier, "frame_generation_multiplier", SettingType::Integer, nullptr, nullptr, &MacroSettings::frameGenerationMultiplier},
        SettingDefinition{SettingId::RpgKey, "rpg_key", SettingType::KeyChord, &MacroSettings::rpgKey},
        SettingDefinition{SettingId::StickyBombKey, "sticky_bomb_key", SettingType::KeyChord, &MacroSettings::stickyBombKey},
        SettingDefinition{SettingId::SniperKey, "sniper_key", SettingType::KeyChord, &MacroSettings::sniperKey},
        SettingDefinition{SettingId::PistolKey, "pistol_key", SettingType::KeyChord, &MacroSettings::pistolKey},
        SettingDefinition{SettingId::ShotgunKey, "shotgun_key", SettingType::KeyChord, &MacroSettings::shotgunKey},
        SettingDefinition{SettingId::RifleKey, "rifle_key", SettingType::KeyChord, &MacroSettings::rifleKey},
        SettingDefinition{SettingId::SmgKey, "smg_key", SettingType::KeyChord, &MacroSettings::smgKey},
        SettingDefinition{SettingId::FistsKey, "fists_key", SettingType::KeyChord, &MacroSettings::fistsKey},
        SettingDefinition{SettingId::MeleeKey, "melee_key", SettingType::KeyChord, &MacroSettings::meleeKey},
        SettingDefinition{SettingId::RpgSpamHotkey, "rpg_spam_hotkey", SettingType::KeyChord, &MacroSettings::rpgSpamHotkey},
        SettingDefinition{SettingId::SniperSpamHotkey, "sniper_spam_hotkey", SettingType::KeyChord, &MacroSettings::sniperSpamHotkey},
        SettingDefinition{SettingId::DoubleSwitchHotkey, "double_switch_hotkey", SettingType::KeyChord, &MacroSettings::doubleSwitchHotkey},
        SettingDefinition{SettingId::TripleSwitchHotkey, "triple_switch_hotkey", SettingType::KeyChord, &MacroSettings::tripleSwitchHotkey},
        SettingDefinition{SettingId::RpgTabSwitchHotkey, "rpg_tab_switch_hotkey", SettingType::KeyChord, &MacroSettings::rpgTabSwitchHotkey},
        SettingDefinition{SettingId::StickyBombTabSwitchHotkey, "sticky_bomb_tab_switch_hotkey", SettingType::KeyChord, &MacroSettings::stickyBombTabSwitchHotkey},
        SettingDefinition{SettingId::SniperTabSwitchHotkey, "sniper_tab_switch_hotkey", SettingType::KeyChord, &MacroSettings::sniperTabSwitchHotkey},
        SettingDefinition{SettingId::PistolTabSwitchHotkey, "pistol_tab_switch_hotkey", SettingType::KeyChord, &MacroSettings::pistolTabSwitchHotkey},
        SettingDefinition{SettingId::ShotgunTabSwitchHotkey, "shotgun_tab_switch_hotkey", SettingType::KeyChord, &MacroSettings::shotgunTabSwitchHotkey},
        SettingDefinition{SettingId::RifleTabSwitchHotkey, "rifle_tab_switch_hotkey", SettingType::KeyChord, &MacroSettings::rifleTabSwitchHotkey},
        SettingDefinition{SettingId::SmgTabSwitchHotkey, "smg_tab_switch_hotkey", SettingType::KeyChord, &MacroSettings::smgTabSwitchHotkey},
        SettingDefinition{SettingId::FistsTabSwitchHotkey, "fists_tab_switch_hotkey", SettingType::KeyChord, &MacroSettings::fistsTabSwitchHotkey},
        SettingDefinition{SettingId::MeleeTabSwitchHotkey, "melee_tab_switch_hotkey", SettingType::KeyChord, &MacroSettings::meleeTabSwitchHotkey},
        SettingDefinition{SettingId::SprintKey, "sprint_key", SettingType::KeyChord, &MacroSettings::sprintKey},
        SettingDefinition{SettingId::InteractionMenuKey, "interaction_menu_key", SettingType::KeyChord, &MacroSettings::interactionMenuKey},
        SettingDefinition{SettingId::WeaponWheelKey, "weapon_wheel_key", SettingType::KeyChord, &MacroSettings::weaponWheelKey},
        SettingDefinition{SettingId::ChatKey, "chat_key", SettingType::KeyChord, &MacroSettings::chatKey},
        SettingDefinition{SettingId::UseCursorMacros, "use_cursor_macros", SettingType::Boolean, nullptr, &MacroSettings::useCursorMacros},
        SettingDefinition{SettingId::RepressLeftClick, "repress_left_click", SettingType::Boolean, nullptr, &MacroSettings::repressLeftClick},
        SettingDefinition{SettingId::AutomaticLeftClickHandling, "automatic_left_click_handling", SettingType::Boolean, nullptr, &MacroSettings::automaticLeftClickHandling},
        SettingDefinition{SettingId::AutomaticHorizontalKeyHandling, "automatic_horizontal_key_handling", SettingType::Boolean, nullptr, &MacroSettings::automaticHorizontalKeyHandling},
        SettingDefinition{SettingId::PreciseRtssPolling, "precise_rtss_polling", SettingType::Boolean, nullptr, &MacroSettings::preciseRtssPolling},
        SettingDefinition{SettingId::FrameDetectionCompatibilityMode, "frame_detection_compatibility_mode", SettingType::Boolean, nullptr, &MacroSettings::frameDetectionCompatibilityMode},
        SettingDefinition{SettingId::MaximizeReliability, "maximize_reliability", SettingType::Boolean, nullptr, &MacroSettings::maximizeReliability},
        SettingDefinition{SettingId::ExplicitRpgSwitchHotkey, "explicit_rpg_switch_hotkey", SettingType::KeyChord, &MacroSettings::explicitRpgSwitchHotkey},
        SettingDefinition{SettingId::ExplicitHomingSwitchHotkey, "explicit_homing_switch_hotkey", SettingType::KeyChord, &MacroSettings::explicitHomingSwitchHotkey},
        SettingDefinition{SettingId::ExplicitGrenadeSwitchHotkey, "explicit_grenade_switch_hotkey", SettingType::KeyChord, &MacroSettings::explicitGrenadeSwitchHotkey},
        SettingDefinition{SettingId::SafeHeavySwapHotkey, "safe_heavy_swap_hotkey", SettingType::KeyChord, &MacroSettings::safeHeavySwapHotkey},
        SettingDefinition{SettingId::SuspendHotkey, "suspend_hotkey", SettingType::KeyChord, &MacroSettings::suspendHotkey},
    };
    return definitions;
}

namespace
{
    const SettingDefinition* findSetting(SettingId id)
    {
        for (const auto& definition : settingDefinitions())
        {
            if (definition.id == id) return &definition;
        }
        return nullptr;
    }
}

bool settingIsBoolean(SettingId id)
{
    const auto* definition = findSetting(id);
    return definition && definition->type == SettingType::Boolean;
}

bool settingIsInteger(SettingId id)
{
    const auto* definition = findSetting(id);
    return definition && definition->type == SettingType::Integer;
}

bool runSettingsSelfTest(std::string& error)
{
    auto parsed = KeyChord::parse("Ctrl+Shift+F1");
    if (!parsed || parsed->key != VK_F1 || parsed->modifiers.size() != 2)
    {
        error = "Key chord parsing failed.";
        return false;
    }
    auto roundTrip = KeyChord::parse(parsed->serialize());
    if (!roundTrip || *roundTrip != *parsed)
    {
        error = "Key chord serialization failed.";
        return false;
    }
    MacroSettings defaults = defaultSettings();
    for (const auto& definition : settingDefinitions())
    {
        if (definition.type == SettingType::KeyChord && !settingKey(defaults, definition.id).empty())
        {
            error = "Default settings failed validation.";
            return false;
        }
    }
    if (defaults.quickTurnDegrees != 180 || defaults.frameGenerationMultiplier != 1 || !defaults.useCursorMacros || !defaults.repressLeftClick || defaults.automaticLeftClickHandling || defaults.automaticHorizontalKeyHandling || defaults.preciseRtssPolling || defaults.frameDetectionCompatibilityMode || defaults.maximizeReliability ||
        !settingIsBoolean(SettingId::ThermalNightVision) || !settingIsBoolean(SettingId::FrameDetectionCompatibilityMode) || !settingIsBoolean(SettingId::MaximizeReliability) || settingIsBoolean(SettingId::ThermalHotkey) ||
        settingBool(defaults, SettingId::ThermalNightVision) || !settingIsInteger(SettingId::QuickTurnDegrees) || !settingIsInteger(SettingId::FrameGenerationMultiplier) || settingIsInteger(SettingId::QuickTurnHotkey))
    {
        error = "Default settings failed validation.";
        return false;
    }
    return true;
}

KeyChord& settingKey(MacroSettings& settings, SettingId id)
{
    if (const auto* definition = findSetting(id); definition && definition->type == SettingType::KeyChord)
        return settings.*definition->key;
    static KeyChord unused;
    return unused;
}

const KeyChord& settingKey(const MacroSettings& settings, SettingId id)
{
    return settingKey(const_cast<MacroSettings&>(settings), id);
}

bool& settingBool(MacroSettings& settings, SettingId id)
{
    if (const auto* definition = findSetting(id); definition && definition->type == SettingType::Boolean)
        return settings.*definition->boolean;
    static bool unused = false;
    return unused;
}

bool settingBool(const MacroSettings& settings, SettingId id)
{
    const auto* definition = findSetting(id);
    return definition && definition->type == SettingType::Boolean && settings.*definition->boolean;
}

int& settingInt(MacroSettings& settings, SettingId id)
{
    if (const auto* definition = findSetting(id); definition && definition->type == SettingType::Integer)
        return settings.*definition->integer;
    static int unused = 0;
    return unused;
}

int settingInt(const MacroSettings& settings, SettingId id)
{
    const auto* definition = findSetting(id);
    return definition && definition->type == SettingType::Integer ? settings.*definition->integer : 0;
}

std::filesystem::path SettingsStore::path()
{
    PWSTR raw = nullptr;
    std::filesystem::path result = L"config.ini";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)))
    {
        result = std::filesystem::path(raw) / L"RTSSReaderMacros" / L"config.ini";
        CoTaskMemFree(raw);
    }
    return result;
}

MacroSettings SettingsStore::load(std::vector<std::string>* warnings)
{
    MacroSettings result = defaultSettings();
    std::ifstream file(path());
    if (!file) return result;
    std::string line;
    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        auto splitAt = line.find('=');
        if (splitAt == std::string::npos) continue;
        std::string name = trim(line.substr(0, splitAt));
        std::string value = trim(line.substr(splitAt + 1));
        std::string normalizedName = lower(name);
        static const std::unordered_map<std::string, std::string> legacyNames = {
            {"weapon_key_1", "pistol_key"}, {"weapon_key_2", "shotgun_key"},
            {"weapon_key_3", "rifle_key"}, {"weapon_key_4", "melee_key"},
            {"weapon_key_5", "fists_key"},
        };
        if (auto legacy = legacyNames.find(normalizedName); legacy != legacyNames.end()) normalizedName = legacy->second;
        const SettingDefinition* definition = nullptr;
        for (const auto& candidate : settingDefinitions())
        {
            if (normalizedName == candidate.name)
            {
                definition = &candidate;
                break;
            }
        }
        if (!definition) continue;
        if (definition->type == SettingType::Boolean)
        {
            settingBool(result, definition->id) = value == "1" || lower(value) == "true";
            continue;
        }
        if (definition->type == SettingType::Integer)
        {
            try
            {
                size_t consumed = 0;
                int parsed = std::stoi(value, &consumed);
                if (consumed != value.size()) throw std::invalid_argument("trailing characters");
                const int maximum = definition->id == SettingId::FrameGenerationMultiplier ? 4 : 360;
                settingInt(result, definition->id) = std::clamp(parsed, 1, maximum);
            }
            catch (...)
            {
                if (warnings) warnings->push_back("Invalid numeric setting: " + name);
            }
            continue;
        }
        auto parsed = KeyChord::parse(value);
        if (parsed) settingKey(result, definition->id) = *parsed;
        else if (warnings) warnings->push_back("Invalid key setting: " + name);
    }
    return result;
}

bool SettingsStore::save(const MacroSettings& settings, std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(path().parent_path(), ec);
    if (ec)
    {
        error = "Could not create the settings directory.";
        return false;
    }
    std::filesystem::path temporary = path();
    temporary += ".tmp";
    std::ofstream file(temporary, std::ios::trunc);
    if (!file)
    {
        error = "Could not write the settings file.";
        return false;
    }
    for (const auto& definition : settingDefinitions())
    {
        file << definition.name << "=";
        if (definition.type == SettingType::Boolean) file << (settings.*definition.boolean ? 1 : 0);
        else if (definition.type == SettingType::Integer) file << (settings.*definition.integer);
        else file << (settings.*definition.key).serialize();
        file << "\n";
    }
    file.close();
    if (!MoveFileExW(temporary.c_str(), path().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = "Could not replace the settings file.";
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

std::vector<GtaProfile> GtaProfileImporter::findProfiles(bool enhanced, std::string& error)
{
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &raw)))
    {
        error = "Could not find the Documents folder.";
        return {};
    }
    std::filesystem::path root(raw);
    CoTaskMemFree(raw);
    root /= L"Rockstar Games";
    root /= (enhanced ? L"GTAV Enhanced" : L"GTA V");
    root /= L"Profiles";
    std::vector<GtaProfile> profiles;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
    {
        error = "The profile directory does not exist.";
        return {};
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, ec))
    {
        if (ec)
        {
            ec.clear();
            continue;
        }
        std::error_code entryError;
        if (!entry.is_directory(entryError) || entryError) continue;
        std::filesystem::path file = entry.path() / L"control" / L"user.xml";
        if (!std::filesystem::is_regular_file(file, entryError) || entryError) continue;
        profiles.push_back({entry.path().filename().wstring(), file, formatFileTime(file)});
    }
    std::sort(profiles.begin(), profiles.end(), [](const GtaProfile& a, const GtaProfile& b)
    {
        std::error_code first, second;
        auto at = std::filesystem::last_write_time(a.file, first);
        auto bt = std::filesystem::last_write_time(b.file, second);
        return !first && !second ? at > bt : a.name < b.name;
    });
    if (profiles.empty()) error = "No profiles containing control\\user.xml were found.";
    return profiles;
}

bool GtaProfileImporter::importProfile(const GtaProfile& profile, MacroSettings& settings,
                                       std::vector<std::string>& warnings, std::string& error)
{
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninitialize = SUCCEEDED(comResult);
    ComPtr<IXMLDOMDocument2> document;
    bool result = loadXmlText(profile.file, document, error);
    if (result)
    {
        importXmlKey(document.Get(), "INPUT_SELECT_WEAPON_SNIPER", settings.sniperKey, warnings, nullptr, "9");
        importXmlKey(document.Get(), "INPUT_SELECT_WEAPON_HEAVY", settings.rpgKey, warnings, nullptr, "4");
        importXmlKey(document.Get(), "INPUT_SELECT_WEAPON_SPECIAL", settings.stickyBombKey, warnings, nullptr, "5");
        importXmlKey(document.Get(), "INPUT_SELECT_WEAPON_HANDGUN", settings.pistolKey, warnings, nullptr, "6");
        importXmlKey(document.Get(), "INPUT_SELECT_WEAPON_SHOTGUN", settings.shotgunKey, warnings, nullptr, "3");
        importXmlKey(document.Get(), "INPUT_SELECT_WEAPON_AUTO_RIFLE", settings.rifleKey, warnings, nullptr, "8");
        importXmlKey(document.Get(), "INPUT_SELECT_WEAPON_SMG", settings.smgKey, warnings, nullptr, "7");
        importXmlKey(document.Get(), "INPUT_SELECT_WEAPON_UNARMED", settings.fistsKey, warnings, nullptr, "1");
        importXmlKey(document.Get(), "INPUT_SELECT_WEAPON_MELEE", settings.meleeKey, warnings, nullptr, "2");
        importXmlKey(document.Get(), "INPUT_SPRINT", settings.sprintKey, warnings, nullptr, "lshift");
        importXmlKey(document.Get(), "INPUT_INTERACTION_MENU", settings.interactionMenuKey, warnings, nullptr, "m");
        importXmlKey(document.Get(), "INPUT_SELECT_WEAPON", settings.weaponWheelKey, warnings, nullptr, "tab");
        importXmlKey(document.Get(), "INPUT_MP_TEXT_CHAT_ALL", settings.chatKey, warnings, nullptr, "t");
    }
    if (shouldUninitialize) CoUninitialize();
    return result;
}
