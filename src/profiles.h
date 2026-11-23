#pragma once

#include "types.h"

#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

std::filesystem::path GetExeDirectory();
std::filesystem::path GetDefaultConfigPath();
std::filesystem::path GetScreenshotsDirectory();
FilterParams BuildCuratedPreset(int presetIdx);
const wchar_t* GetCuratedPresetName(int presetIdx);

struct ForegroundAppInfo {
    std::wstring exeName;
    std::wstring displayName;
    bool isGame = false;
    std::wstring engineTag;
    HWND hwnd = nullptr;
};

std::string WideToUtf8(const std::wstring& w);
std::wstring Utf8ToWide(const std::string& s);
std::wstring NormalizeExeFilename(const std::wstring& pathOrName);
std::wstring FormatHotkeyString(uint32_t mods, uint32_t vk);
bool IsTransientCaptureProcess(const std::wstring& exeLower);

bool ClassifyProcessAsGame(
    const std::filesystem::path& exePath,
    const std::wstring& windowClass,
    bool isBorderlessFullscreen,
    std::wstring& outEngineTag,
    const std::vector<std::wstring>* customWhitelist = nullptr,
    const std::vector<std::wstring>* customBlacklist = nullptr
);
bool DetectActiveForegroundApp(
    DWORD selfPid,
    ForegroundAppInfo& outInfo,
    const std::vector<std::wstring>* customWhitelist = nullptr,
    const std::vector<std::wstring>* customBlacklist = nullptr
);

bool SaveConfig(const AppConfig& cfg, const std::filesystem::path& path);
bool LoadConfig(AppConfig& cfg, const std::filesystem::path& path);
bool ExportPresetFile(const FilterParams& preset, const std::wstring& presetName, const std::filesystem::path& path);
bool ImportPresetFile(FilterParams& outPreset, std::wstring& outName, const std::filesystem::path& path);

bool SetLaunchAtStartup(bool enable);
bool IsLaunchAtStartupEnabled();
bool IsExclusiveFullscreenActive();
