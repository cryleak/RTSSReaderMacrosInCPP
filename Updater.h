#pragma once

#include <string>

namespace Updater
{
    inline constexpr char kCurrentVersion[] = "1.0.0";

    struct UpdateInfo
    {
        bool ok = false;
        bool available = false;
        std::string currentVersion = kCurrentVersion;
        std::string latestVersion;
        std::wstring assetUrl;
        std::string error;
    };

    struct InstallResult
    {
        bool ok = false;
        std::string error;
    };

    UpdateInfo checkForUpdate();
    InstallResult downloadAndInstall(const UpdateInfo& info);
    bool selfTest(std::string& error);
} // namespace Updater
