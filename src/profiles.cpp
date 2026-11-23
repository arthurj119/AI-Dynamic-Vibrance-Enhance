#include "profiles.h"

#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

std::filesystem::path GetExeDirectory() {
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buf).parent_path();
}

std::filesystem::path GetDefaultConfigPath() {
    return GetExeDirectory() / L"profiles.json";
}

std::filesystem::path GetScreenshotsDirectory() {
    PWSTR picRaw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &picRaw)) && picRaw) {
        std::filesystem::path dir = std::filesystem::path(picRaw) / L"AI Dynamic Vibrance Enhance";
        CoTaskMemFree(picRaw);
        return dir;
    }
    return GetExeDirectory() / L"Screenshots";
}

const wchar_t* GetCuratedPresetName(int presetIdx) {
    switch (presetIdx) {
        case 0:  return L"Balanced RTX";
        case 1:  return L"Cinematic Film";
        case 2:  return L"Competitive";
        case 3:  return L"Vibrant RPG";
        case 4:  return L"Night Comfort";
        default: return L"Balanced RTX";
    }
}

FilterParams BuildCuratedPreset(int presetIdx) {
    FilterParams p{};
    switch (presetIdx) {
        case 0: // Balanced RTX
            return p;
        case 1: // Cinematic Film (AgX + Soft Bloom + AO + Deband)
            p.intensity = 50;
            p.saturationBoost = 50;
            p.enableFilmic = true;
            p.filmicMode = 1; // AgX
            p.filmicStrength = 30;
            p.enableBloom = true;
            p.bloomIntensity = 20;
            p.bloomThreshold = 68;
            p.enableAmbientOcclusion = true;
            p.ambientOcclusion = 30;
            p.aoRadius = 50;
            p.enableDeband = true;
            p.debandStrength = 35;
            return p;
        case 2: // Competitive (Dark Visibility + Sharpen+)
            p.intensity = 60;
            p.saturationBoost = 60;
            p.enableDarkLighting = true;
            p.darkLighting = 35;
            p.enableSharpen = true;
            p.sharpen = 45;
            return p;
        case 3: // Vibrant RPG (Rich Color + Micro-Contrast + Deband)
            p.intensity = 65;
            p.saturationBoost = 70;
            p.enableClarity = true;
            p.clarity = 25;
            p.enableDeband = true;
            p.debandStrength = 45;
            return p;
        case 4: // Night Comfort (Relaxed Vibrance + Blue-Light Reduction)
            p.intensity = 40;
            p.saturationBoost = 35;
            p.enableNightMode = true;
            p.nightMode = 35;
            return p;
        default:
            return p;
    }
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

static std::wstring ToLowerW(std::wstring s) {
    for (wchar_t& ch : s) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return s;
}

bool IsTransientCaptureProcess(const std::wstring& exeLower) {
    static const std::unordered_set<std::wstring> kTransientCaptureProcs = {
        L"screenclippinghost.exe",
        L"snippingtool.exe",
        L"sharex.exe",
        L"lightshot.exe",
        L"greenshot.exe",
        L"gamebar.exe",
        L"gamebarftserver.exe",
        L"amddvr.exe",
        L"nvidia share.exe"
    };
    return kTransientCaptureProcs.find(NormalizeExeFilename(exeLower)) != kTransientCaptureProcs.end();
}

std::wstring NormalizeExeFilename(const std::wstring& pathOrName) {
    if (pathOrName.empty()) return {};
    std::wstring fname = ToLowerW(std::filesystem::path(pathOrName).filename().wstring());
    size_t start = fname.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return {};
    size_t end = fname.find_last_not_of(L" \t\r\n");
    return fname.substr(start, end - start + 1);
}

std::wstring FormatHotkeyString(uint32_t mods, uint32_t vk) {
    if (vk == 0) return L"None";
    std::wstring s;
    if (mods & MOD_CONTROL) s += L"Ctrl + ";
    if (mods & MOD_ALT)     s += L"Alt + ";
    if (mods & MOD_SHIFT)   s += L"Shift + ";
    if (mods & MOD_WIN)     s += L"Win + ";

    std::wstring key;
    if (vk >= 'A' && vk <= 'Z') {
        key = std::wstring(1, static_cast<wchar_t>(vk));
    } else if (vk >= '0' && vk <= '9') {
        key = std::wstring(1, static_cast<wchar_t>(vk));
    } else if (vk >= VK_F1 && vk <= VK_F12) {
        key = L"F" + std::to_wstring(vk - VK_F1 + 1);
    } else {
        switch (vk) {
            case VK_INSERT: key = L"Ins"; break;
            case VK_DELETE: key = L"Del"; break;
            case VK_HOME:   key = L"Home"; break;
            case VK_END:    key = L"End"; break;
            case VK_PRIOR:  key = L"PgUp"; break;
            case VK_NEXT:   key = L"PgDn"; break;
            case VK_OEM_3:  key = L"`"; break;
            case VK_TAB:    key = L"Tab"; break;
            case VK_SPACE:  key = L"Space"; break;
            case VK_PAUSE:  key = L"Pause"; break;
            case VK_SCROLL: key = L"ScrLk"; break;
            default: {
                UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                wchar_t buf[64] = {};
                if (sc != 0 && GetKeyNameTextW(static_cast<LONG>(sc << 16), buf, 64) > 0) {
                    key = buf;
                } else {
                    key = L"Key " + std::to_wstring(vk);
                }
                break;
            }
        }
    }
    return s + key;
}

static bool EndsWithW(const std::wstring& s, const wchar_t* suffix) {
    size_t slen = wcslen(suffix);
    return s.size() >= slen && s.compare(s.size() - slen, slen, suffix) == 0;
}

bool ClassifyProcessAsGame(
    const std::filesystem::path& exePath,
    const std::wstring& windowClass,
    bool isBorderlessFullscreen,
    std::wstring& outEngineTag,
    const std::vector<std::wstring>* customWhitelist,
    const std::vector<std::wstring>* customBlacklist
) {
    if (exePath.empty()) {
        outEngineTag = L"Non-Game";
        return false;
    }

    std::wstring exeLower = ToLowerW(exePath.filename().wstring());
    std::wstring fullLower = ToLowerW(exePath.wstring());
    std::wstring clsLower = ToLowerW(windowClass);

    // User-configured overrides take highest priority over built-in detection rules
    if (customBlacklist) {
        for (const auto& entry : *customBlacklist) {
            if (NormalizeExeFilename(entry) == exeLower) {
                outEngineTag = L"Ignored (User)";
                return false;
            }
        }
    }
    if (customWhitelist) {
        for (const auto& entry : *customWhitelist) {
            if (NormalizeExeFilename(entry) == exeLower) {
                outEngineTag = L"Custom Game";
                return true;
            }
        }
    }

    static const std::unordered_set<std::wstring> kNonGameBlacklist = {
        // Browsers
        L"chrome.exe", L"msedge.exe", L"firefox.exe", L"brave.exe", L"opera.exe", L"opera_gx.exe",
        L"vivaldi.exe", L"waterfox.exe", L"librewolf.exe", L"arc.exe", L"thorium.exe",
        // Windows shell / OS / GPU overlays
        L"explorer.exe", L"dwm.exe", L"applicationframehost.exe", L"systemsettings.exe",
        L"taskmgr.exe", L"mmc.exe", L"regedit.exe", L"control.exe", L"msiexec.exe",
        L"snippingtool.exe", L"screenclippinghost.exe", L"photos.exe", L"microsoft.photos.exe",
        L"textinputhost.exe", L"searchhost.exe", L"shellexperiencehost.exe",
        L"startmenuexperiencehost.exe", L"lockapp.exe",
        L"radeonsoftware.exe", L"amdrsserv.exe", L"nvidia app.exe", L"nvidia overlay.exe", L"nvcontainer.exe",
        // Chat / Media / Office / Dev tools
        L"discord.exe", L"telegram.exe", L"whatsapp.exe", L"slack.exe", L"teams.exe", L"ms-teams.exe",
        L"spotify.exe", L"vlc.exe", L"mpv.exe", L"mpc-hc64.exe", L"mpc-be64.exe", L"potplayermini64.exe",
        L"obs64.exe", L"streamlabs obs.exe",
        L"code.exe", L"devenv.exe", L"idea64.exe", L"pycharm64.exe", L"notepad.exe", L"notepad++.exe",
        L"winword.exe", L"excel.exe", L"powerpnt.exe", L"onenote.exe", L"acrobat.exe",
        L"cmd.exe", L"powershell.exe", L"pwsh.exe", L"windowsterminal.exe", L"conhost.exe",
        L"python.exe", L"node.exe", L"antigravity.exe",
        // Game launchers & store clients (launchers themselves are not gameplay)
        L"steam.exe", L"steamwebhelper.exe", L"epicgameslauncher.exe", L"epicwebhelper.exe",
        L"eadesktop.exe", L"eabackgroundservice.exe", L"upc.exe", L"ubisoftconnect.exe",
        L"battle.net.exe", L"galaxyclient.exe", L"riotclientux.exe", L"riotclientservices.exe",
        L"hoyolauncher.exe", L"hyp.exe", L"xboxpcapp.exe", L"playnite.desktopapp.exe", L"wechat.exe"
    };

    // Step A: Non-Game Blacklist
    if (kNonGameBlacklist.find(exeLower) != kNonGameBlacklist.end() ||
        fullLower.rfind(L"c:\\windows\\", 0) == 0 ||
        fullLower.find(L"\\google\\chrome\\") != std::wstring::npos ||
        fullLower.find(L"\\microsoft\\edge\\") != std::wstring::npos) {
        outEngineTag = L"Non-Game";
        return false;
    }

    // Step B: Fast Window Class Check (zero disk I/O)
    if (clsLower == L"unitywndclass" || clsLower.find(L"unity") != std::wstring::npos) {
        outEngineTag = L"Unity";
        return true;
    }
    if (clsLower == L"unrealwindow" || clsLower.find(L"unreal") != std::wstring::npos) {
        outEngineTag = L"Unreal Engine";
        return true;
    }
    static const std::unordered_set<std::wstring> kKnownGameClasses = {
        L"cryengine", L"glfw30", L"sdl_app", L"valve001", L"lwjgl", L"grcwindow",
        L"w2viewportclass", L"reengine", L"direct3dwindowclass", L"godot_engine",
        L"respawn001", L"frostbite", L"snowdrop", L"creationengine", L"rage"
    };
    if (kKnownGameClasses.find(clsLower) != kKnownGameClasses.end() ||
        clsLower.find(L"witcher") != std::wstring::npos ||
        clsLower.find(L"reengine") != std::wstring::npos) {
        outEngineTag = L"Game Engine";
        return true;
    }

    // Step C: Path & Directory Pattern Check (cached once per unique executable path)
    static std::unordered_map<std::wstring, std::pair<bool, std::wstring>> s_pathCache;
    auto it = s_pathCache.find(fullLower);
    if (it == s_pathCache.end()) {
        bool detected = false;
        std::wstring tag = L"Non-Game";

        if (EndsWithW(exeLower, L"-win64-shipping.exe") ||
            EndsWithW(exeLower, L"-wingdk-shipping.exe") ||
            fullLower.find(L"\\binaries\\win64\\") != std::wstring::npos ||
            fullLower.find(L"\\binaries\\win32\\") != std::wstring::npos) {
            detected = true;
            tag = L"Unreal Engine";
        } else {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path exeDir = exePath.parent_path();
            std::wstring exeStem = exePath.stem().wstring();
            if (!exeDir.empty() && fs::exists(exeDir, ec) &&
                (fs::exists(exeDir / L"UnityPlayer.dll", ec) ||
                 (!exeStem.empty() && fs::exists(exeDir / (exeStem + L"_Data"), ec)))) {
                detected = true;
                tag = L"Unity";
            }
        }

        if (!detected) {
            static const wchar_t* kLibraryMarkers[] = {
                L"\\steamapps\\common\\", L"\\epic games\\", L"\\gog games\\", L"\\xboxgames\\",
                L"\\riot games\\", L"\\ubisoft game launcher\\games\\", L"\\ea games\\",
                L"\\origin games\\", L"\\hoyoplay\\games\\", L"\\genshin impact",
                L"\\wuthering waves", L"\\honkai", L"\\zenlesszonezero",
                L"\\rockstar games\\", L"\\games\\"
            };
            for (const wchar_t* marker : kLibraryMarkers) {
                if (fullLower.find(marker) != std::wstring::npos) {
                    detected = true;
                    tag = L"Game Library";
                    break;
                }
            }
        }

        if (!detected) {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path d0 = exePath.parent_path();
            fs::path d1 = d0.parent_path();
            fs::path d2 = d1.parent_path();
            const fs::path searchDirs[3] = { d0, d1, d2 };
            static const wchar_t* kRuntimeDlls[] = {
                L"steam_api64.dll", L"steam_api.dll", L"EOSSDK-Win64-Shipping.dll", L"Galaxy64.dll",
                L"nvngx_dlss.dll", L"nvngx_dlssg.dll", L"amd_fidelityfx_dx12.dll", L"amd_fidelityfx_vk.dll",
                L"ffx_fsr2_api_x64.dll", L"libxess.dll", L"dstorage.dll", L"bink2w64.dll",
                L"fmod64.dll", L"fmodstudio.dll", L"PhysX3_x64.dll", L"oo2core_8_win64.dll",
                L"oo2core_9_win64.dll", L"sl.interposer.dll", L"AkSoundEngine.dll",
                L"GFSDK_Aftermath_Lib.x64.dll", L"AnselSDK64.dll", L"XAudio2_7.dll"
            };
            for (const fs::path& dir : searchDirs) {
                if (dir.empty() || !fs::exists(dir, ec)) continue;
                for (const wchar_t* dll : kRuntimeDlls) {
                    if (fs::exists(dir / dll, ec)) {
                        detected = true;
                        tag = L"Game Engine";
                        break;
                    }
                }
                if (detected) break;
            }
        }

        it = s_pathCache.emplace(fullLower, std::make_pair(detected, tag)).first;
    }

    if (it->second.first) {
        outEngineTag = it->second.second;
        return true;
    }

    // Step D: Non-blacklisted borderless / fullscreen fallback (indie / retro titles)
    if (isBorderlessFullscreen) {
        outEngineTag = L"Fullscreen Game";
        return true;
    }

    outEngineTag = L"Non-Game";
    return false;
}

bool DetectActiveForegroundApp(
    DWORD selfPid,
    ForegroundAppInfo& outInfo,
    const std::vector<std::wstring>* customWhitelist,
    const std::vector<std::wstring>* customBlacklist
) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == selfPid) return false;

    wchar_t classBuf[128] = {};
    GetClassNameW(hwnd, classBuf, 128);
    std::wstring cls(classBuf);
    if (cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd" ||
        cls == L"Progman" || cls == L"WorkerW" || cls == L"Windows.UI.Core.CoreWindow") {
        outInfo = { L"desktop", L"Desktop", false, L"Desktop", nullptr };
        return true;
    }

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return false;

    wchar_t pathBuf[MAX_PATH * 2] = {};
    DWORD pathLen = static_cast<DWORD>(std::size(pathBuf));
    BOOL ok = QueryFullProcessImageNameW(proc, 0, pathBuf, &pathLen);
    CloseHandle(proc);
    if (!ok || pathLen == 0) return false;

    std::filesystem::path exePath(pathBuf);
    std::wstring exeLower = ToLowerW(exePath.filename().wstring());

    if (IsTransientCaptureProcess(exeLower)) {
        return false; // Preserve current targetHwnd and WGC/overlay state during screenshots
    }

    if (exeLower == L"explorer.exe" || exeLower == L"searchhost.exe" ||
        exeLower == L"shellexperiencehost.exe" || exeLower == L"startmenuexperiencehost.exe" ||
        exeLower == L"lockapp.exe") {
        outInfo = { L"desktop", L"Desktop", false, L"Desktop", nullptr };
        return true;
    }

    bool isBorderlessFullscreen = false;
    RECT wr{};
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetWindowRect(hwnd, &wr) && hMon && GetMonitorInfoW(hMon, &mi)) {
        LONG monW = mi.rcMonitor.right - mi.rcMonitor.left;
        LONG monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
        LONG winW = wr.right - wr.left;
        LONG winH = wr.bottom - wr.top;
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        if (winW >= (monW - 4) && winH >= (monH - 4) && (style & WS_CAPTION) == 0) {
            isBorderlessFullscreen = true;
        }
    }

    wchar_t titleBuf[256] = {};
    GetWindowTextW(hwnd, titleBuf, 256);
    std::wstring title(titleBuf);

    outInfo.exeName = exeLower;
    if (!title.empty()) {
        if (title.size() > 34) {
            title = title.substr(0, 31) + L"...";
        }
        outInfo.displayName = title;
    } else {
        outInfo.displayName = exePath.stem().wstring();
    }
    outInfo.hwnd = hwnd;
    outInfo.isGame = ClassifyProcessAsGame(
        exePath, cls, isBorderlessFullscreen, outInfo.engineTag,
        customWhitelist, customBlacklist
    );
    return true;
}

namespace {

std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool bVal = false;
    double nVal = 0.0;
    std::string sVal;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    const JsonValue* Get(const std::string& k) const {
        if (type != Object) return nullptr;
        auto it = obj.find(k);
        return (it != obj.end()) ? &it->second : nullptr;
    }
    int AsInt(int def) const {
        return (type == Number) ? static_cast<int>(std::lround(nVal)) : def;
    }
    float AsFloat(float def) const {
        return (type == Number) ? static_cast<float>(nVal) : def;
    }
    bool AsBool(bool def) const {
        return (type == Bool) ? bVal : def;
    }
    std::string AsString(const std::string& def = {}) const {
        return (type == String) ? sVal : def;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string src) : src_(std::move(src)) {}

    bool Parse(JsonValue& out) {
        SkipWs();
        if (!ParseValue(out)) return false;
        SkipWs();
        return pos_ == src_.size();
    }

private:
    std::string src_;
    size_t pos_ = 0;

    void SkipWs() {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    bool Consume(char expected) {
        SkipWs();
        if (pos_ < src_.size() && src_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool ParseString(std::string& out) {
        SkipWs();
        if (pos_ >= src_.size() || src_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= src_.size()) return false;
                char e = src_[pos_++];
                switch (e) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) return false;
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = src_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned int>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned int>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned int>(h - 'A' + 10);
                            else return false;
                        }
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseValue(JsonValue& out) {
        SkipWs();
        if (pos_ >= src_.size()) return false;
        char c = src_[pos_];
        if (c == '"') {
            out = JsonValue{};
            out.type = JsonValue::String;
            return ParseString(out.sVal);
        }
        if (c == '{') {
            out = JsonValue{};
            out.type = JsonValue::Object;
            ++pos_;
            SkipWs();
            if (pos_ < src_.size() && src_[pos_] == '}') {
                ++pos_;
                return true;
            }
            while (true) {
                std::string key;
                if (!ParseString(key)) return false;
                if (!Consume(':')) return false;
                JsonValue val;
                if (!ParseValue(val)) return false;
                out.obj[key] = std::move(val);
                SkipWs();
                if (pos_ < src_.size() && src_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                return Consume('}');
            }
        }
        if (c == '[') {
            out = JsonValue{};
            out.type = JsonValue::Array;
            ++pos_;
            SkipWs();
            if (pos_ < src_.size() && src_[pos_] == ']') {
                ++pos_;
                return true;
            }
            while (true) {
                JsonValue elem;
                if (!ParseValue(elem)) return false;
                out.arr.push_back(std::move(elem));
                SkipWs();
                if (pos_ < src_.size() && src_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                return Consume(']');
            }
        }
        if (src_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out = JsonValue{};
            out.type = JsonValue::Bool;
            out.bVal = true;
            return true;
        }
        if (src_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out = JsonValue{};
            out.type = JsonValue::Bool;
            out.bVal = false;
            return true;
        }
        if (src_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out = JsonValue{};
            out.type = JsonValue::Null;
            return true;
        }
        // Number
        const char* start = src_.c_str() + pos_;
        char* endPtr = nullptr;
        double d = std::strtod(start, &endPtr);
        if (endPtr == start) return false;
        pos_ = static_cast<size_t>(endPtr - src_.c_str());
        out = JsonValue{};
        out.type = JsonValue::Number;
        out.nVal = d;
        return true;
    }
};

void WriteParams(std::ostringstream& ss, const FilterParams& p, const char* indent) {
    ss << "{\n";
    ss << indent << "  \"intensity\": " << p.intensity << ",\n";
    ss << indent << "  \"saturationBoost\": " << p.saturationBoost << ",\n";
    ss << indent << "  \"darkLighting\": " << p.darkLighting << ",\n";
    ss << indent << "  \"clarity\": " << p.clarity << ",\n";
    ss << indent << "  \"dynamicHdr\": " << p.dynamicHdr << ",\n";
    ss << indent << "  \"hdrPeakNits\": " << p.hdrPeakNits << ",\n";
    ss << indent << "  \"hdrMidGray\": " << p.hdrMidGray << ",\n";
    ss << indent << "  \"brightness\": " << p.brightness << ",\n";
    ss << indent << "  \"contrast\": " << p.contrast << ",\n";
    ss << indent << "  \"sharpen\": " << p.sharpen << ",\n";
    ss << indent << "  \"temperature\": " << p.temperature << ",\n";
    ss << indent << "  \"tint\": " << p.tint << ",\n";
    ss << indent << "  \"exposure\": " << p.exposure << ",\n";
    ss << indent << "  \"gamma\": " << p.gamma << ",\n";
    ss << indent << "  \"nightMode\": " << p.nightMode << ",\n";
    ss << indent << "  \"blackAndWhite\": " << p.blackAndWhite << ",\n";
    ss << indent << "  \"vignette\": " << p.vignette << ",\n";
    ss << indent << "  \"colorblindStrength\": " << p.colorblindStrength << ",\n";
    ss << indent << "  \"colorblindType\": " << p.colorblindType << ",\n";
    ss << indent << "  \"ambientOcclusion\": " << p.ambientOcclusion << ",\n";
    ss << indent << "  \"aoRadius\": " << p.aoRadius << ",\n";
    ss << indent << "  \"bloomIntensity\": " << p.bloomIntensity << ",\n";
    ss << indent << "  \"bloomThreshold\": " << p.bloomThreshold << ",\n";
    ss << indent << "  \"filmicStrength\": " << p.filmicStrength << ",\n";
    ss << indent << "  \"filmicMode\": " << p.filmicMode << ",\n";
    ss << indent << "  \"debandStrength\": " << p.debandStrength << ",\n";
    ss << indent << "  \"enableDynamicVibrance\": " << (p.enableDynamicVibrance ? "true" : "false") << ",\n";
    ss << indent << "  \"enableDarkLighting\": " << (p.enableDarkLighting ? "true" : "false") << ",\n";
    ss << indent << "  \"enableAmbientOcclusion\": " << (p.enableAmbientOcclusion ? "true" : "false") << ",\n";
    ss << indent << "  \"enableClarity\": " << (p.enableClarity ? "true" : "false") << ",\n";
    ss << indent << "  \"enableDynamicHdr\": " << (p.enableDynamicHdr ? "true" : "false") << ",\n";
    ss << indent << "  \"enableBrightnessContrast\": " << (p.enableBrightnessContrast ? "true" : "false") << ",\n";
    ss << indent << "  \"enableSharpen\": " << (p.enableSharpen ? "true" : "false") << ",\n";
    ss << indent << "  \"enableColorTemp\": " << (p.enableColorTemp ? "true" : "false") << ",\n";
    ss << indent << "  \"enableGammaExposure\": " << (p.enableGammaExposure ? "true" : "false") << ",\n";
    ss << indent << "  \"enableNightMode\": " << (p.enableNightMode ? "true" : "false") << ",\n";
    ss << indent << "  \"enableBlackAndWhite\": " << (p.enableBlackAndWhite ? "true" : "false") << ",\n";
    ss << indent << "  \"enableVignette\": " << (p.enableVignette ? "true" : "false") << ",\n";
    ss << indent << "  \"enableColorblind\": " << (p.enableColorblind ? "true" : "false") << ",\n";
    ss << indent << "  \"enableBloom\": " << (p.enableBloom ? "true" : "false") << ",\n";
    ss << indent << "  \"enableFilmic\": " << (p.enableFilmic ? "true" : "false") << ",\n";
    ss << indent << "  \"enableDeband\": " << (p.enableDeband ? "true" : "false") << "\n";
    ss << indent << "}";
}

void ReadParams(const JsonValue& v, FilterParams& p) {
    if (v.type != JsonValue::Object) return;
    auto clamp100 = [](int x) { return std::clamp(x, 0, 100); };
    if (auto* x = v.Get("intensity")) p.intensity = clamp100(x->AsInt(p.intensity));
    if (auto* x = v.Get("saturationBoost")) p.saturationBoost = clamp100(x->AsInt(p.saturationBoost));
    if (auto* x = v.Get("darkLighting")) p.darkLighting = clamp100(x->AsInt(p.darkLighting));
    if (auto* x = v.Get("clarity")) p.clarity = clamp100(x->AsInt(p.clarity));
    if (auto* x = v.Get("dynamicHdr")) p.dynamicHdr = clamp100(x->AsInt(p.dynamicHdr));
    if (auto* x = v.Get("hdrPeakNits")) p.hdrPeakNits = std::clamp(x->AsInt(p.hdrPeakNits), 300, 1500);
    if (auto* x = v.Get("hdrMidGray")) p.hdrMidGray = clamp100(x->AsInt(p.hdrMidGray));
    if (auto* x = v.Get("brightness")) p.brightness = clamp100(x->AsInt(p.brightness));
    if (auto* x = v.Get("contrast")) p.contrast = clamp100(x->AsInt(p.contrast));
    if (auto* x = v.Get("sharpen")) p.sharpen = clamp100(x->AsInt(p.sharpen));
    if (auto* x = v.Get("temperature")) p.temperature = clamp100(x->AsInt(p.temperature));
    if (auto* x = v.Get("tint")) p.tint = clamp100(x->AsInt(p.tint));
    if (auto* x = v.Get("exposure")) p.exposure = clamp100(x->AsInt(p.exposure));
    if (auto* x = v.Get("gamma")) p.gamma = clamp100(x->AsInt(p.gamma));
    if (auto* x = v.Get("nightMode")) p.nightMode = clamp100(x->AsInt(p.nightMode));
    if (auto* x = v.Get("blackAndWhite")) p.blackAndWhite = clamp100(x->AsInt(p.blackAndWhite));
    if (auto* x = v.Get("vignette")) p.vignette = clamp100(x->AsInt(p.vignette));
    if (auto* x = v.Get("colorblindStrength")) p.colorblindStrength = clamp100(x->AsInt(p.colorblindStrength));
    if (auto* x = v.Get("colorblindType")) p.colorblindType = std::clamp(x->AsInt(p.colorblindType), 0, 2);
    if (auto* x = v.Get("ambientOcclusion")) p.ambientOcclusion = clamp100(x->AsInt(p.ambientOcclusion));
    if (auto* x = v.Get("aoRadius")) p.aoRadius = clamp100(x->AsInt(p.aoRadius));
    if (auto* x = v.Get("bloomIntensity")) p.bloomIntensity = clamp100(x->AsInt(p.bloomIntensity));
    if (auto* x = v.Get("bloomThreshold")) p.bloomThreshold = clamp100(x->AsInt(p.bloomThreshold));
    if (auto* x = v.Get("filmicStrength")) p.filmicStrength = clamp100(x->AsInt(p.filmicStrength));
    if (auto* x = v.Get("filmicMode")) p.filmicMode = std::clamp(x->AsInt(p.filmicMode), 0, 2);
    if (auto* x = v.Get("debandStrength")) p.debandStrength = clamp100(x->AsInt(p.debandStrength));
    p.skinProtection = true;
    p.adaptiveScene = true;
    if (auto* x = v.Get("enableDynamicVibrance")) p.enableDynamicVibrance = x->AsBool(p.enableDynamicVibrance);
    if (auto* x = v.Get("enableDarkLighting")) p.enableDarkLighting = x->AsBool(p.enableDarkLighting);
    if (auto* x = v.Get("enableAmbientOcclusion")) p.enableAmbientOcclusion = x->AsBool(p.enableAmbientOcclusion);
    if (auto* x = v.Get("enableClarity")) p.enableClarity = x->AsBool(p.enableClarity);
    if (auto* x = v.Get("enableDynamicHdr")) p.enableDynamicHdr = x->AsBool(p.enableDynamicHdr);
    if (auto* x = v.Get("enableBrightnessContrast")) p.enableBrightnessContrast = x->AsBool(p.enableBrightnessContrast);
    if (auto* x = v.Get("enableSharpen")) p.enableSharpen = x->AsBool(p.enableSharpen);
    if (auto* x = v.Get("enableColorTemp")) p.enableColorTemp = x->AsBool(p.enableColorTemp);
    if (auto* x = v.Get("enableGammaExposure")) p.enableGammaExposure = x->AsBool(p.enableGammaExposure);
    if (auto* x = v.Get("enableNightMode")) p.enableNightMode = x->AsBool(p.enableNightMode);
    if (auto* x = v.Get("enableBlackAndWhite")) p.enableBlackAndWhite = x->AsBool(p.enableBlackAndWhite);
    if (auto* x = v.Get("enableVignette")) p.enableVignette = x->AsBool(p.enableVignette);
    if (auto* x = v.Get("enableColorblind")) p.enableColorblind = x->AsBool(p.enableColorblind);
    if (auto* x = v.Get("enableBloom")) p.enableBloom = x->AsBool(p.enableBloom);
    if (auto* x = v.Get("enableFilmic")) p.enableFilmic = x->AsBool(p.enableFilmic);
    if (auto* x = v.Get("enableDeband")) p.enableDeband = x->AsBool(p.enableDeband);
}

} // namespace

bool SaveConfig(const AppConfig& cfg, const std::filesystem::path& path) {
    auto writeStrArray = [](std::ostringstream& ss, const char* name, const std::vector<std::wstring>& items, bool comma) {
        ss << "  \"" << name << "\": [";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "\"" << EscapeJson(WideToUtf8(items[i])) << "\"";
        }
        ss << "]" << (comma ? ",\n" : "\n");
    };

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"schemaVersion\": " << cfg.schemaVersion << ",\n";
    ss << "  \"activeProfile\": " << cfg.activeProfile << ",\n";
    ss << "  \"overlayEnabled\": " << (cfg.overlayEnabled ? "true" : "false") << ",\n";
    ss << "  \"autoSwitchPerApp\": " << (cfg.autoSwitchPerApp ? "true" : "false") << ",\n";
    ss << "  \"onlyInGames\": " << (cfg.onlyInGames ? "true" : "false") << ",\n";
    ss << "  \"launchAtStartup\": " << (cfg.launchAtStartup ? "true" : "false") << ",\n";
    ss << "  \"mode2Enabled\": " << (cfg.mode2Enabled ? "true" : "false") << ",\n";
    ss << "  \"splitPreview\": " << (cfg.splitPreview ? "true" : "false") << ",\n";
    ss << "  \"splitPosition\": " << cfg.splitPosition << ",\n";
    ss << "  \"hubListView\": " << (cfg.hubListView ? "true" : "false") << ",\n";
    ss << "  \"hideSidebarFromCapture\": " << (cfg.hideSidebarFromCapture ? "true" : "false") << ",\n";
    ss << "  \"sidebarHotkeyMods\": " << cfg.sidebarHotkeyMods << ",\n";
    ss << "  \"sidebarHotkeyVk\": " << cfg.sidebarHotkeyVk << ",\n";
    ss << "  \"quickToggleEnabled\": " << (cfg.quickToggleEnabled ? "true" : "false") << ",\n";
    ss << "  \"quickToggleMods\": " << cfg.quickToggleMods << ",\n";
    ss << "  \"quickToggleVk\": " << cfg.quickToggleVk << ",\n";
    ss << "  \"screenshotHotkeyMods\": " << cfg.screenshotHotkeyMods << ",\n";
    ss << "  \"screenshotHotkeyVk\": " << cfg.screenshotHotkeyVk << ",\n";
    ss << "  \"mode2ExportProfile\": " << std::clamp(cfg.mode2ExportProfile, 1, 3) << ",\n";
    ss << "  \"reshadeFolder\": \"" << EscapeJson(WideToUtf8(cfg.reshadeFolder)) << "\",\n";
    ss << "  \"scaleMode\": " << std::clamp(cfg.scaleMode, 0, 4) << ",\n";
    ss << "  \"scaleSharpness\": " << std::clamp(cfg.scaleSharpness, 0, 100) << ",\n";
    ss << "  \"scaleFullscreenStretch\": " << (cfg.scaleFullscreenStretch ? "true" : "false") << ",\n";
    ss << "  \"fgMode\": " << std::clamp(cfg.fgMode, 0, 4) << ",\n";
    ss << "  \"fgFlowScale\": " << std::clamp(cfg.fgFlowScale, 25, 100) << ",\n";
    ss << "  \"fgAdaptiveTargetFps\": " << std::clamp(cfg.fgAdaptiveTargetFps, 60, 240) << ",\n";
    ss << "  \"fgFastMotionFallback\": " << (cfg.fgFastMotionFallback ? "true" : "false") << ",\n";
    ss << "  \"fgStaticHudProtect\": " << (cfg.fgStaticHudProtect ? "true" : "false") << ",\n";
    ss << "  \"captureApi\": " << std::clamp(cfg.captureApi, 0, 1) << ",\n";
    ss << "  \"allowTearing\": " << (cfg.allowTearing ? "true" : "false") << ",\n";
    ss << "  \"profiles\": [\n";
    for (int i = 0; i < 3; ++i) {
        ss << "    ";
        WriteParams(ss, cfg.profiles[i], "    ");
        ss << (i < 2 ? ",\n" : "\n");
    }
    ss << "  ],\n";
    ss << "  \"perAppProfile\": {\n";
    size_t idx = 0;
    for (const auto& kv : cfg.perAppProfile) {
        ss << "    \"" << EscapeJson(WideToUtf8(kv.first)) << "\": " << kv.second;
        ss << (++idx < cfg.perAppProfile.size() ? ",\n" : "\n");
    }
    ss << "  },\n";
    writeStrArray(ss, "customGameWhitelist", cfg.customGameWhitelist, true);
    writeStrArray(ss, "customGameBlacklist", cfg.customGameBlacklist, false);
    ss << "}\n";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    std::string str = ss.str();
    ofs.write(str.data(), static_cast<std::streamsize>(str.size()));
    return static_cast<bool>(ofs);
}

bool LoadConfig(AppConfig& cfg, const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::ostringstream buf;
    buf << ifs.rdbuf();
    JsonValue root;
    JsonParser parser(buf.str());
    if (!parser.Parse(root) || root.type != JsonValue::Object) {
        return false;
    }

    int loadedVersion = 1;
    if (auto* x = root.Get("schemaVersion")) loadedVersion = x->AsInt(1);

    if (auto* x = root.Get("activeProfile")) cfg.activeProfile = std::clamp(x->AsInt(cfg.activeProfile), 0, 3);
    if (auto* x = root.Get("overlayEnabled")) cfg.overlayEnabled = x->AsBool(cfg.overlayEnabled);
    if (auto* x = root.Get("autoSwitchPerApp")) cfg.autoSwitchPerApp = x->AsBool(cfg.autoSwitchPerApp);
    if (auto* x = root.Get("onlyInGames")) cfg.onlyInGames = x->AsBool(cfg.onlyInGames);
    if (auto* x = root.Get("launchAtStartup")) cfg.launchAtStartup = x->AsBool(cfg.launchAtStartup);
    if (auto* x = root.Get("mode2Enabled")) cfg.mode2Enabled = x->AsBool(cfg.mode2Enabled);
    if (auto* x = root.Get("splitPreview")) cfg.splitPreview = x->AsBool(cfg.splitPreview);
    if (auto* x = root.Get("splitPosition")) cfg.splitPosition = std::clamp(x->AsFloat(cfg.splitPosition), 0.05f, 0.95f);
    if (auto* x = root.Get("hubListView")) cfg.hubListView = x->AsBool(cfg.hubListView);
    if (auto* x = root.Get("hideSidebarFromCapture")) cfg.hideSidebarFromCapture = x->AsBool(cfg.hideSidebarFromCapture);
    if (auto* x = root.Get("sidebarHotkeyMods")) cfg.sidebarHotkeyMods = static_cast<uint32_t>(std::max(0, x->AsInt(static_cast<int>(cfg.sidebarHotkeyMods))));
    if (auto* x = root.Get("sidebarHotkeyVk")) {
        int vk = x->AsInt(static_cast<int>(cfg.sidebarHotkeyVk));
        cfg.sidebarHotkeyVk = (vk > 0 && vk < 256) ? static_cast<uint32_t>(vk) : 0x58u;
    }
    if (cfg.sidebarHotkeyVk == 0) {
        cfg.sidebarHotkeyMods = MOD_ALT;
        cfg.sidebarHotkeyVk = 'X';
    }
    if (auto* x = root.Get("quickToggleEnabled")) cfg.quickToggleEnabled = x->AsBool(cfg.quickToggleEnabled);
    if (auto* x = root.Get("quickToggleMods")) cfg.quickToggleMods = static_cast<uint32_t>(std::max(0, x->AsInt(static_cast<int>(cfg.quickToggleMods))));
    if (auto* x = root.Get("quickToggleVk")) {
        int vk = x->AsInt(static_cast<int>(cfg.quickToggleVk));
        cfg.quickToggleVk = (vk > 0 && vk < 256) ? static_cast<uint32_t>(vk) : 0x56u;
    }
    if (cfg.quickToggleVk == 0) {
        cfg.quickToggleMods = MOD_ALT;
        cfg.quickToggleVk = 'V';
    }
    if (auto* x = root.Get("screenshotHotkeyMods")) cfg.screenshotHotkeyMods = static_cast<uint32_t>(std::max(0, x->AsInt(static_cast<int>(cfg.screenshotHotkeyMods))));
    if (auto* x = root.Get("screenshotHotkeyVk")) {
        int vk = x->AsInt(static_cast<int>(cfg.screenshotHotkeyVk));
        cfg.screenshotHotkeyVk = (vk > 0 && vk < 256) ? static_cast<uint32_t>(vk) : 0x7Bu;
    }
    if (cfg.screenshotHotkeyVk == 0) {
        cfg.screenshotHotkeyMods = MOD_ALT;
        cfg.screenshotHotkeyVk = VK_F12;
    }
    if (auto* x = root.Get("mode2ExportProfile")) cfg.mode2ExportProfile = std::clamp(x->AsInt(cfg.mode2ExportProfile), 1, 3);
    if (auto* x = root.Get("reshadeFolder")) cfg.reshadeFolder = Utf8ToWide(x->AsString());
    if (auto* x = root.Get("scaleMode")) cfg.scaleMode = std::clamp(x->AsInt(cfg.scaleMode), 0, 4);
    if (auto* x = root.Get("scaleSharpness")) cfg.scaleSharpness = std::clamp(x->AsInt(cfg.scaleSharpness), 0, 100);
    if (auto* x = root.Get("scaleFullscreenStretch")) cfg.scaleFullscreenStretch = x->AsBool(cfg.scaleFullscreenStretch);
    if (auto* x = root.Get("fgMode")) cfg.fgMode = std::clamp(x->AsInt(cfg.fgMode), 0, 4);
    if (auto* x = root.Get("fgFlowScale")) cfg.fgFlowScale = std::clamp(x->AsInt(cfg.fgFlowScale), 25, 100);
    if (auto* x = root.Get("fgAdaptiveTargetFps")) cfg.fgAdaptiveTargetFps = std::clamp(x->AsInt(cfg.fgAdaptiveTargetFps), 60, 240);
    if (auto* x = root.Get("fgFastMotionFallback")) cfg.fgFastMotionFallback = x->AsBool(cfg.fgFastMotionFallback);
    if (auto* x = root.Get("fgStaticHudProtect")) cfg.fgStaticHudProtect = x->AsBool(cfg.fgStaticHudProtect);
    if (auto* x = root.Get("captureApi")) cfg.captureApi = std::clamp(x->AsInt(cfg.captureApi), 0, 1);
    if (auto* x = root.Get("allowTearing")) cfg.allowTearing = x->AsBool(cfg.allowTearing);
    if (loadedVersion >= 2) {
        cfg.schemaVersion = 3;
        if (auto* arr = root.Get("profiles")) {
            if (arr->type == JsonValue::Array) {
                for (size_t i = 0; i < std::min<size_t>(3, arr->arr.size()); ++i) {
                    ReadParams(arr->arr[i], cfg.profiles[i]);
                }
            }
        }
    } else {
        // Migrate v1 over-stacked presets to clean v3 modular defaults
        cfg.schemaVersion = 3;
        for (int i = 0; i < 3; ++i) {
            cfg.profiles[i] = FilterParams{};
        }
    }
    if (auto* m = root.Get("perAppProfile")) {
        if (m->type == JsonValue::Object) {
            cfg.perAppProfile.clear();
            for (const auto& kv : m->obj) {
                cfg.perAppProfile[ToLowerW(Utf8ToWide(kv.first))] = std::clamp(kv.second.AsInt(1), 0, 3);
            }
        }
    }
    auto readExeList = [](const JsonValue* arr, std::vector<std::wstring>& dst) {
        if (!arr || arr->type != JsonValue::Array) return;
        dst.clear();
        std::unordered_set<std::wstring> seen;
        for (const auto& elem : arr->arr) {
            if (elem.type != JsonValue::String) continue;
            std::wstring norm = NormalizeExeFilename(Utf8ToWide(elem.sVal));
            if (!norm.empty() && seen.insert(norm).second) {
                dst.push_back(std::move(norm));
            }
        }
    };
    readExeList(root.Get("customGameWhitelist"), cfg.customGameWhitelist);
    readExeList(root.Get("customGameBlacklist"), cfg.customGameBlacklist);
    return true;
}

bool ExportPresetFile(const FilterParams& preset, const std::wstring& presetName, const std::filesystem::path& path) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"format\": \"AIDynamicVibranceEnhancePreset\",\n";
    ss << "  \"version\": 3,\n";
    ss << "  \"name\": \"" << EscapeJson(WideToUtf8(presetName)) << "\",\n";
    ss << "  \"params\": ";
    WriteParams(ss, preset, "  ");
    ss << "\n}\n";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    std::string str = ss.str();
    ofs.write(str.data(), static_cast<std::streamsize>(str.size()));
    return static_cast<bool>(ofs);
}

bool ImportPresetFile(FilterParams& outPreset, std::wstring& outName, const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::ostringstream buf;
    buf << ifs.rdbuf();
    JsonValue root;
    JsonParser parser(buf.str());
    if (!parser.Parse(root) || root.type != JsonValue::Object) {
        return false;
    }

    outName.clear();
    if (auto* nm = root.Get("name")) {
        outName = Utf8ToWide(nm->AsString());
    }
    if (auto* paramsObj = root.Get("params")) {
        if (paramsObj->type != JsonValue::Object) return false;
        FilterParams fp{};
        ReadParams(*paramsObj, fp);
        outPreset = fp;
        return true;
    }
    if (root.Get("intensity") != nullptr || root.Get("saturationBoost") != nullptr) {
        FilterParams fp{};
        ReadParams(root, fp);
        outPreset = fp;
        return true;
    }
    return false;
}

bool SetLaunchAtStartup(bool enable) {
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &hKey, nullptr
    );
    if (rc != ERROR_SUCCESS || !hKey) return false;

    bool ok = false;
    if (enable) {
        wchar_t exeBuf[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::wstring cmd = std::wstring(L"\"") + exeBuf + L"\" --minimized";
            DWORD bytes = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
            ok = (RegSetValueExW(hKey, L"AIDynamicVibranceEnhance", 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(cmd.c_str()), bytes) == ERROR_SUCCESS);
        }
    } else {
        LSTATUS delRc = RegDeleteValueW(hKey, L"AIDynamicVibranceEnhance");
        ok = (delRc == ERROR_SUCCESS || delRc == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(hKey);
    return ok;
}

bool IsLaunchAtStartupEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS || !hKey) {
        return false;
    }
    DWORD type = 0, size = 0;
    LSTATUS rc = RegQueryValueExW(hKey, L"AIDynamicVibranceEnhance", nullptr, &type, nullptr, &size);
    RegCloseKey(hKey);
    return (rc == ERROR_SUCCESS && type == REG_SZ && size > 0);
}

bool IsExclusiveFullscreenActive() {
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        return state == QUNS_RUNNING_D3D_FULL_SCREEN;
    }
    return false;
}
