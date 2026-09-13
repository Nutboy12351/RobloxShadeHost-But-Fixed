// RobloxShadeHost: redraws the Roblox window in a D3D11 swapchain of its own, so ReShade can be
// installed on this exe instead of Roblox. Roblox is only observed from outside, through window
// enumeration and Windows.Graphics.Capture. Nothing is opened, read or loaded into its process.

#include <unknwn.h>
#include <windows.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
#include <string>

#include "hotkey.h"

using namespace winrt::Windows::Graphics::Capture;
using winrt::Windows::Foundation::Metadata::ApiInformation;
using winrt::Windows::Graphics::SizeInt32;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

namespace
{
constexpr wchar_t kRobloxExe[] = L"RobloxPlayerBeta.exe";
constexpr auto kPixelFormat = DirectXPixelFormat::B8G8R8A8UIntNormalized;
constexpr int kEditModeHotkey = 1;

// WS_EX_LAYERED + WS_EX_TRANSPARENT is what makes clicks reach Roblox. Returning HTTRANSPARENT from
// WM_NCHITTEST only passes input to windows owned by the same thread.
constexpr DWORD kPassThroughStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
constexpr DWORD kEditStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED;

struct State
{
    HWND overlay = nullptr;
    HWND target = nullptr;
    HWND indicator = nullptr;
    std::wstring inputHotkey = L"Ctrl+Home";
    std::wstring indicatorText;
    bool editMode = false;
    bool overlayVisible = false;
    RECT overlayRect{};

    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    winrt::com_ptr<IDXGISwapChain1> swapchain;
    IDirect3DDevice captureDevice{ nullptr };

    Direct3D11CaptureFramePool pool{ nullptr };
    GraphicsCaptureSession session{ nullptr };
    Direct3D11CaptureFramePool::FrameArrived_revoker frameArrived;
    Direct3D11CaptureFrame latestFrame{ nullptr };
    SizeInt32 poolSize{};
    HANDLE frameEvent = nullptr;
} g;

Hotkey LoadInputHotkey()
{
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    winrt::check_bool(length && length < std::size(executable));
    const std::wstring path = std::wstring(executable).substr(0, std::wstring(executable).find_last_of(L"\\/") + 1)
                              + L"RobloxShadeHost.ini";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        winrt::check_bool(WritePrivateProfileStringW(L"Input", L"ToggleKey", g.inputHotkey.c_str(), path.c_str()));

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
    return hotkey;
}

LRESULT CALLBACK IndicatorWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        HBRUSH background = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(dc, &rect, background);
        DeleteObject(background);
        HFONT font = CreateFontW(-MulDiv(14, GetDpiForWindow(hwnd), 96), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ previous = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 218, 128));
        DrawTextW(dc, g.indicatorText.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previous);
        DeleteObject(font);
        EndPaint(hwnd, &paint);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND FindRobloxWindow()
{
    std::vector<DWORD> pids;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return nullptr;
    PROCESSENTRY32W entry{ sizeof(entry) };
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
    {
        if (_wcsicmp(entry.szExeFile, kRobloxExe) == 0)
            pids.push_back(entry.th32ProcessID);
    }
    CloseHandle(snapshot);
    if (pids.empty())
        return nullptr;

    struct Search
    {
        const std::vector<DWORD>& pids;
        HWND found = nullptr;
    } search{ pids };

    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (std::find(search.pids.begin(), search.pids.end(), pid) == search.pids.end())
                return TRUE;
            if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) || (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
                return TRUE;
            search.found = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&search));

    return search.found;
}

void CreateDevice()
{
    winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                           D3D11_SDK_VERSION, g.device.put(), nullptr, g.context.put()));

    // The capture pool uses the device from its own worker threads.
    g.device.as<ID3D11Multithread>()->SetMultithreadProtected(TRUE);

    auto dxgiDevice = g.device.as<IDXGIDevice1>();
    dxgiDevice->SetMaximumFrameLatency(1);

    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    g.captureDevice = inspectable.as<IDirect3DDevice>();
}

void StartCapture(HWND target)
{
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{ nullptr };
    winrt::check_hresult(interop->CreateForWindow(target, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));

    g.poolSize = item.Size();
    g.pool = Direct3D11CaptureFramePool::CreateFreeThreaded(g.captureDevice, kPixelFormat, 2, g.poolSize);
    g.frameArrived = g.pool.FrameArrived(winrt::auto_revoke, [](auto&&, auto&&) { SetEvent(g.frameEvent); });
    g.session = g.pool.CreateCaptureSession(item);

    // The real cursor is already drawn on top of the overlay.
    g.session.IsCursorCaptureEnabled(false);
    if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired"))
    {
        GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless);
        g.session.IsBorderRequired(false);
    }
    // Without this, capture can be capped at 60 FPS.
    if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"MinUpdateInterval"))
        g.session.MinUpdateInterval(std::chrono::milliseconds(1));

    g.session.StartCapture();
    g.target = target;
    std::printf("Capturing Roblox (%dx%d)\n", g.poolSize.Width, g.poolSize.Height);
}

void SetEditMode(bool enabled)
{
    if (enabled == g.editMode)
        return;
    g.editMode = enabled;
    SetWindowLongPtrW(g.overlay, GWL_EXSTYLE, enabled ? kEditStyle : kPassThroughStyle);
    SetWindowPos(g.overlay, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (!enabled)
        ShowWindow(g.indicator, SW_HIDE);
    if (enabled)
        std::printf("Input captured. Use your ReShade menu key to open its menu; %ls returns to Roblox.\n", g.inputHotkey.c_str());
    else
        std::puts("Input returned to Roblox.");
}

void StopCapture()
{
    SetEditMode(false);
    g.frameArrived.revoke();
    g.latestFrame = nullptr;
    g.session.Close();
    g.session = nullptr;
    g.pool.Close();
    g.pool = nullptr;
    g.target = nullptr;
}

// Keeps the overlay exactly over Roblox while Roblox is the foreground window (or while editing).
void UpdateOverlay()
{
    RECT bounds{};
    bool visible = g.target && IsWindowVisible(g.target) && !IsIconic(g.target) &&
                   (g.editMode || GetForegroundWindow() == g.target) &&
                   SUCCEEDED(DwmGetWindowAttribute(g.target, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)));

    if (!visible)
    {
        if (g.overlayVisible)
            ShowWindow(g.overlay, SW_HIDE);
        g.overlayVisible = false;
        ShowWindow(g.indicator, SW_HIDE);
        return;
    }

    if (!g.overlayVisible || !EqualRect(&bounds, &g.overlayRect))
    {
        SetWindowPos(g.overlay, HWND_TOPMOST, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        g.overlayRect = bounds;
        g.overlayVisible = true;
    }
    if (g.editMode)
    {
        const UINT dpi = GetDpiForWindow(g.overlay);
        const int margin = MulDiv(12, dpi, 96);
        const int height = MulDiv(36, dpi, 96);
        // Keep the badge above the swapchain, without taking focus or blocking clicks.
        const int width = std::min<int>(bounds.right - bounds.left, MulDiv(560, dpi, 96));
        SetWindowPos(g.indicator, HWND_TOPMOST, bounds.left + (bounds.right - bounds.left - width) / 2,
                     bounds.bottom - height - margin, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void PresentLatestFrame()
{
    winrt::com_ptr<ID3D11Texture2D> surface;
    auto access = g.latestFrame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), surface.put_void()));
    D3D11_TEXTURE2D_DESC size{};
    surface->GetDesc(&size);

    if (!g.swapchain)
    {
        winrt::com_ptr<IDXGIAdapter> adapter;
        winrt::check_hresult(g.device.as<IDXGIDevice>()->GetAdapter(adapter.put()));
        winrt::com_ptr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()));

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = size.Width;
        desc.Height = size.Height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        winrt::check_hresult(factory->CreateSwapChainForHwnd(g.device.get(), g.overlay, &desc, nullptr, nullptr, g.swapchain.put()));
        factory->MakeWindowAssociation(g.overlay, DXGI_MWA_NO_ALT_ENTER);
    }
    else
    {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        g.swapchain->GetDesc1(&desc);
        if (desc.Width != size.Width || desc.Height != size.Height)
            winrt::check_hresult(g.swapchain->ResizeBuffers(0, size.Width, size.Height, DXGI_FORMAT_UNKNOWN, 0));
    }

    winrt::com_ptr<ID3D11Texture2D> backBuffer;
    winrt::check_hresult(g.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.put_void()));
    g.context->CopyResource(backBuffer.get(), surface.get());
    winrt::check_hresult(g.swapchain->Present(0, 0));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_HOTKEY:
        if (wParam != kEditModeHotkey)
            break;
        if (g.editMode)
        {
            SetEditMode(false);
            SetForegroundWindow(g.target);
        }
        else if (g.target && !IsIconic(g.target))
        {
            SetEditMode(true);
            UpdateOverlay();
            SetForegroundWindow(hwnd);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
            SetEditMode(false);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int Run()
{
    const Hotkey inputHotkey = LoadInputHotkey();
    if (!GraphicsCaptureSession::IsSupported())
    {
        std::puts("Windows Graphics Capture is not supported on this system.");
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"RobloxShadeHost";
    RegisterClassW(&wc);

    g.overlay = CreateWindowExW(kPassThroughStyle, wc.lpszClassName, L"RobloxShadeHost", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                wc.hInstance, nullptr);
    winrt::check_bool(g.overlay != nullptr);
    SetLayeredWindowAttributes(g.overlay, 0, 255, LWA_ALPHA);

    WNDCLASSW indicatorClass{};
    indicatorClass.lpfnWndProc = IndicatorWndProc;
    indicatorClass.hInstance = wc.hInstance;
    indicatorClass.lpszClassName = L"RobloxShadeHostInputIndicator";
    winrt::check_bool(RegisterClassW(&indicatorClass));
    g.indicator = CreateWindowExW(kPassThroughStyle, indicatorClass.lpszClassName, L"Input captured", WS_POPUP,
                                  0, 0, 1, 1, g.overlay, nullptr, wc.hInstance, nullptr);
    winrt::check_bool(g.indicator != nullptr);
    winrt::check_bool(SetLayeredWindowAttributes(g.indicator, 0, 255, LWA_ALPHA));

    if (!RegisterHotKey(g.overlay, kEditModeHotkey, inputHotkey.modifiers, inputHotkey.key))
    {
        const std::wstring error = L"Could not register " + g.inputHotkey + L". It may be reserved by Windows or in use by another program. "
                                   L"Choose another ToggleKey in RobloxShadeHost.ini and restart.";
        MessageBoxW(nullptr, error.c_str(), L"RobloxShadeHost", MB_OK | MB_ICONERROR);
        return 1;
    }

    g.frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CreateDevice();

    std::puts("Install ReShade on this exe (DirectX 10/11/12).\n"
              "Waiting for Roblox...");
    std::printf("%ls: toggle input capture. ReShade keeps its own menu and effect shortcuts.\n", g.inputHotkey.c_str());

    ULONGLONG nextSearch = 0;
    for (;;)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (g.target && !IsWindow(g.target))
        {
            StopCapture();
            std::puts("Roblox closed. Waiting for Roblox...");
        }

        if (!g.target && GetTickCount64() >= nextSearch)
        {
            nextSearch = GetTickCount64() + 500;
            if (HWND roblox = FindRobloxWindow())
            {
                try
                {
                    StartCapture(roblox);
                }
                catch (const winrt::hresult_error& e)
                {
                    std::printf("Could not capture Roblox: %ls\n", e.message().c_str());
                }
            }
        }

        UpdateOverlay();

        if (g.target)
        {
            // Only the newest frame matters. Rendering every queued frame would add latency.
            while (auto frame = g.pool.TryGetNextFrame())
                g.latestFrame = frame;

            if (g.latestFrame)
            {
                SizeInt32 size = g.latestFrame.ContentSize();
                if ((size.Width != g.poolSize.Width || size.Height != g.poolSize.Height) && size.Width > 0 && size.Height > 0)
                {
                    g.latestFrame = nullptr;
                    g.poolSize = size;
                    g.pool.Recreate(g.captureDevice, kPixelFormat, 2, size);
                    std::printf("Roblox resized to %dx%d\n", size.Width, size.Height);
                }
            }

            // Also re-presents on timeout, so the ReShade menu stays responsive if Roblox stops drawing.
            if (g.overlayVisible && g.latestFrame)
                PresentLatestFrame();
        }

        MsgWaitForMultipleObjects(1, &g.frameEvent, FALSE, g.overlayVisible ? 16 : 250, QS_ALLINPUT);
    }
}
} // namespace

int main()
{
    std::puts(R"(  ____       _     _            ____  _               _      _   _           _
 |  _ \ ___ | |__ | | _____  __/ ___|| |__   __ _  __| | ___| | | | ___  ___| |_
 | |_) / _ \| '_ \| |/ _ \ \/ /\___ \| '_ \ / _` |/ _` |/ _ \ |_| |/ _ \/ __| __|
 |  _ < (_) | |_) | | (_) >  <  ___) | | | | (_| | (_| |  __/  _  | (_) \__ \ |_
 |_| \_\___/|_.__/|_|\___/_/\_\|____/|_| |_|\__,_|\__,_|\___|_| |_|\___/|___/\__|
)");
    std::printf("v%s\n\n", ROBLOX_SHADE_HOST_VERSION);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try
    {
        return Run();
    }
    catch (const winrt::hresult_error& e)
    {
        std::printf("Error 0x%08X: %ls\n", static_cast<unsigned>(e.code()), e.message().c_str());
        return 1;
    }
}
