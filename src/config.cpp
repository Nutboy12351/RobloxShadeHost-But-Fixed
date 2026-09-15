#include "config.h"
#include "state.h"

#include <cstdlib>
#include <cwchar>
#include <iterator>

namespace
{
std::wstring ConfigPath()
{
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    winrt::check_bool(length && length < std::size(executable));
    return std::wstring(executable).substr(0, std::wstring(executable).find_last_of(L"\\/") + 1) + L"RobloxShadeHost.ini";
}

COLORREF ReadColor(const wchar_t* section, const wchar_t* key, COLORREF fallback, const std::wstring& path)
{
    wchar_t value[32]{};
    const DWORD count = GetPrivateProfileStringW(section, key, L"", value, static_cast<DWORD>(std::size(value)), path.c_str());
    if (!count || count == std::size(value) - 1)
        return fallback;
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(value + (value[0] == L'#'), &end, 16);
    if (*end != L'\0' || parsed > 0xFFFFFF)
        return fallback;
    return RGB((parsed >> 16) & 0xFF, (parsed >> 8) & 0xFF, parsed & 0xFF);
}

void WriteDefault(const wchar_t* section, const wchar_t* key, const wchar_t* value, const std::wstring& path)
{
    wchar_t current[2]{};
    if (!GetPrivateProfileStringW(section, key, L"", current, static_cast<DWORD>(std::size(current)), path.c_str()))
        winrt::check_bool(WritePrivateProfileStringW(section, key, value, path.c_str()));
}
} // namespace

Hotkey LoadInputHotkey()
{
    const std::wstring path = ConfigPath();
    WriteDefault(L"Input", L"ToggleKey", g.inputHotkey.c_str(), path);
    WriteDefault(L"Console", L"Title", L"RobloxShadeHost", path);
    WriteDefault(L"Console", L"Name", L"RobloxShadeHost", path);
    WriteDefault(L"Console", L"Version", L"", path);
    WriteDefault(L"Console", L"TextColor", L"DCE6EB", path);
    WriteDefault(L"Indicator", L"BackgroundColor", L"181C22", path);
    WriteDefault(L"Indicator", L"TextColor", L"EBF2F0", path);
    WriteDefault(L"Indicator", L"AccentColor", L"53BE9C", path);

    wchar_t value[128]{};
    const DWORD count = GetPrivateProfileStringW(L"Input", L"ToggleKey", L"Ctrl+Home", value, static_cast<DWORD>(std::size(value)), path.c_str());
    Hotkey hotkey;
    if (count == std::size(value) - 1 || !ParseHotkey(value, hotkey))
    {
        MessageBoxW(nullptr, L"Invalid ToggleKey in RobloxShadeHost.ini. Use a key such as Ctrl+Home or F8. See the README for supported keys.",
                    L"RobloxShadeHost", MB_OK | MB_ICONERROR);
        winrt::throw_hresult(E_INVALIDARG);
    }
    g.inputHotkey = value;
    g.indicatorText = L"Input captured | " + g.inputHotkey + L" to return to Roblox";

    wchar_t text[256]{};
    GetPrivateProfileStringW(L"Console", L"Title", L"RobloxShadeHost", text, static_cast<DWORD>(std::size(text)), path.c_str());
    g.consoleTitle = text;
    GetPrivateProfileStringW(L"Console", L"Name", L"RobloxShadeHost", text, static_cast<DWORD>(std::size(text)), path.c_str());
    g.consoleName = text;
    GetPrivateProfileStringW(L"Console", L"Version", L"", text, static_cast<DWORD>(std::size(text)), path.c_str());
    g.consoleVersion = text;
    g.consoleTextColor = ReadColor(L"Console", L"TextColor", g.consoleTextColor, path);
    g.indicatorBackground = ReadColor(L"Indicator", L"BackgroundColor", g.indicatorBackground, path);
    g.indicatorTextColor = ReadColor(L"Indicator", L"TextColor", g.indicatorTextColor, path);
    g.indicatorAccent = ReadColor(L"Indicator", L"AccentColor", g.indicatorAccent, path);
    return hotkey;
}
