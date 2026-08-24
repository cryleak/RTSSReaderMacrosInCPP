#pragma once

#include <Windows.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct KeyChord
{
    WORD key = 0;
    std::vector<WORD> modifiers;

    bool empty() const { return key == 0; }
    bool operator==(const KeyChord& other) const;
    std::string serialize() const;
    std::wstring displayName() const;

    static std::optional<KeyChord> parse(const std::string& value);
};

enum class SettingId
{
    BstHotkey,
    ThermalHotkey,
    ThermalNightVision,
    SnacksHotkey,
    AmmoHotkey,
    QuickTurnHotkey,
    QuickTurnDegrees,
    FrameGenerationMultiplier,
    RpgKey,
    StickyBombKey,
    SniperKey,
    PistolKey,
    ShotgunKey,
    RifleKey,
    SmgKey,
    FistsKey,
    MeleeKey,
    RpgSpamHotkey,
    SniperSpamHotkey,
    DoubleSwitchHotkey,
    TripleSwitchHotkey,
    RpgTabSwitchHotkey,
    StickyBombTabSwitchHotkey,
    SniperTabSwitchHotkey,
    PistolTabSwitchHotkey,
    ShotgunTabSwitchHotkey,
    RifleTabSwitchHotkey,
    SmgTabSwitchHotkey,
    FistsTabSwitchHotkey,
    MeleeTabSwitchHotkey,
    SprintKey,
    InteractionMenuKey,
    WeaponWheelKey,
    ChatKey,
    UseCursorMacros,
    RepressLeftClick,
    AutomaticLeftClickHandling,
    AutomaticHorizontalKeyHandling,
    ExplicitRpgSwitchHotkey,
    ExplicitHomingSwitchHotkey,
    ExplicitGrenadeSwitchHotkey,
    SafeHeavySwapHotkey,
    SuspendHotkey,
};

struct MacroSettings
{
    KeyChord bstHotkey;
    KeyChord thermalHotkey;
    bool thermalNightVision = false;
    KeyChord snacksHotkey;
    KeyChord ammoHotkey;
    KeyChord quickTurnHotkey;
    int quickTurnDegrees = 180;
    int frameGenerationMultiplier = 1;

    KeyChord rpgKey;
    KeyChord stickyBombKey;
    KeyChord sniperKey;
    KeyChord pistolKey;
    KeyChord shotgunKey;
    KeyChord rifleKey;
    KeyChord smgKey;
    KeyChord fistsKey;
    KeyChord meleeKey;
    KeyChord rpgSpamHotkey;
    KeyChord sniperSpamHotkey;
    KeyChord doubleSwitchHotkey;
    KeyChord tripleSwitchHotkey;
    KeyChord rpgTabSwitchHotkey;
    KeyChord stickyBombTabSwitchHotkey;
    KeyChord sniperTabSwitchHotkey;
    KeyChord pistolTabSwitchHotkey;
    KeyChord shotgunTabSwitchHotkey;
    KeyChord rifleTabSwitchHotkey;
    KeyChord smgTabSwitchHotkey;
    KeyChord fistsTabSwitchHotkey;
    KeyChord meleeTabSwitchHotkey;

    KeyChord sprintKey;
    KeyChord interactionMenuKey;
    KeyChord weaponWheelKey;
    KeyChord chatKey;

    bool useCursorMacros = true;
    bool repressLeftClick = true;
    bool automaticLeftClickHandling = false;
    bool automaticHorizontalKeyHandling = false;
    KeyChord explicitRpgSwitchHotkey;
    KeyChord explicitHomingSwitchHotkey;
    KeyChord explicitGrenadeSwitchHotkey;
    KeyChord safeHeavySwapHotkey;
    KeyChord suspendHotkey;
};

enum class SettingType { KeyChord, Boolean, Integer };

struct SettingDefinition
{
    SettingId id;
    const char* name;
    SettingType type;
    KeyChord MacroSettings::* key = nullptr;
    bool MacroSettings::* boolean = nullptr;
    int MacroSettings::* integer = nullptr;
};

MacroSettings defaultSettings();
const std::vector<SettingDefinition>& settingDefinitions();
bool settingIsBoolean(SettingId id);
bool settingIsInteger(SettingId id);
KeyChord& settingKey(MacroSettings& settings, SettingId id);
const KeyChord& settingKey(const MacroSettings& settings, SettingId id);
bool& settingBool(MacroSettings& settings, SettingId id);
bool settingBool(const MacroSettings& settings, SettingId id);
int& settingInt(MacroSettings& settings, SettingId id);
int settingInt(const MacroSettings& settings, SettingId id);
bool runSettingsSelfTest(std::string& error);

struct GtaProfile
{
    std::wstring name;
    std::filesystem::path file;
    std::wstring modified;
};

class SettingsStore
{
public:
    static std::filesystem::path path();
    static MacroSettings load(std::vector<std::string>* warnings = nullptr);
    static bool save(const MacroSettings& settings, std::string& error);
};

class GtaProfileImporter
{
public:
    static std::vector<GtaProfile> findProfiles(bool enhanced, std::string& error);
    static bool importProfile(const GtaProfile& profile, MacroSettings& settings,
                              std::vector<std::string>& warnings, std::string& error);
};
