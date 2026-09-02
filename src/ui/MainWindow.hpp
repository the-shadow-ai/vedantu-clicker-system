#pragma once
// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include "ui/UiState.hpp"
#include "hid/ClickerClient.hpp"
#include "logging/Logger.hpp"
#include "logging/LatencyLogger.hpp"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
// clang-format on

namespace svs {

#define WM_TRAYICON (WM_USER + 100)
#define TRAY_UID    1001
#define IDM_RESTORE 2001
#define IDM_EXIT    2002

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Colour tokens  (unchanged â€” same brand palette)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static inline ImVec4 hex(unsigned r, unsigned g, unsigned b, float a = 1.f) {
  return {r / 255.f, g / 255.f, b / 255.f, a};
}

static constexpr ImVec4 kBg0          = {0.051f, 0.067f, 0.090f, 1.f}; // #0D1117
static constexpr ImVec4 kBg1          = {0.110f, 0.137f, 0.184f, 1.f}; // #1C2330
static constexpr ImVec4 kBg2          = {0.149f, 0.180f, 0.235f, 1.f}; // #262E3C
static constexpr ImVec4 kBg3          = {0.180f, 0.216f, 0.275f, 1.f}; // #2E3746
static constexpr ImVec4 kOrange       = {1.000f, 0.420f, 0.000f, 1.f}; // #FF6B00
static constexpr ImVec4 kOrangeHov    = {1.000f, 0.533f, 0.094f, 1.f}; // #FF8818
static constexpr ImVec4 kOrangeAct    = {0.855f, 0.322f, 0.000f, 1.f}; // #DA5200
static constexpr ImVec4 kGreen        = {0.125f, 0.718f, 0.361f, 1.f}; // #20B75C
static constexpr ImVec4 kRed          = {0.914f, 0.173f, 0.173f, 1.f}; // #E92C2C
static constexpr ImVec4 kAmber        = {0.953f, 0.596f, 0.000f, 1.f}; // #F39800
static constexpr ImVec4 kTextPrimary  = {0.925f, 0.933f, 0.945f, 1.f}; // #ECEEF1
static constexpr ImVec4 kTextSecondary= {0.608f, 0.635f, 0.690f, 1.f}; // #9BA2B0
static constexpr ImVec4 kTextDisabled = {0.396f, 0.420f, 0.467f, 1.f}; // #656B77
static constexpr ImVec4 kBorder       = {0.220f, 0.255f, 0.318f, 1.f}; // #384151
static constexpr ImVec4 kBorderAcc    = {1.000f, 0.420f, 0.000f, 0.55f};

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  UI scale constants â€” sized for 75"/85" touch screens
//  All fonts are double the original size.
//  All panel/button heights scale with window height so the layout fills
//  any aspect ratio correctly (maximize or landscape resize).
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static constexpr float kFontRegular = 36.f;  // was 18
static constexpr float kFontLarge   = 48.f;  // was 24
static constexpr float kFontSmall   = 28.f;  // was 14
static constexpr float kFontTable   = 40.f;  // was 20

// Long-press paste threshold (seconds).
// 500 ms fires well before Windows touch-and-hold gesture interception (~750 ms),
// so the paste completes before the OS steals the touch as a right-click.
static constexpr float kLongPressSec = 0.5f;

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
class MainWindow {
public:
  using ValidateCallback  = std::function<void(std::string)>;
  using RegisterStartStop = std::function<void(bool)>;

  MainWindow(UiState &state, ValidateCallback on_validate,
             RegisterStartStop on_reg_toggle)
      : state_(state), on_validate_(std::move(on_validate)),
        on_reg_toggle_(std::move(on_reg_toggle)) {}

  ~MainWindow() { destroy(); }

  bool create(const std::string &title) {
    // Must be before CreateWindow so OS knows we handle DPI.
    // Without this, Windows bilinearly upscales our framebuffer at >100% DPI.
    ImGui_ImplWin32_EnableDpiAwareness();
    hwnd_title_ = title;
    // Use 70% of the physical screen so the window fits both laptop
    // displays and 75"/85" smartboards without needing manual maximize.
    int scr_w = ::GetSystemMetrics(SM_CXSCREEN);
    int scr_h = ::GetSystemMetrics(SM_CYSCREEN);
    width_  = static_cast<int>(scr_w * 0.70f);
    height_ = static_cast<int>(scr_h * 0.70f);
    if (!createWindow())  return false;
    if (!createDevice())  return false;
    initImGui();
    return true;
  }

  void runLoop() {
    MSG msg{};
    while (true) {
      // Drain every pending message, including WM_APP+1 wakeups
      // posted by UiState::addEvent() from the clicker thread.
      while (::PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageA(&msg);
        if (msg.message == WM_QUIT)
          return;
      }
      render();
      // Sleep ≤1 ms — PostMessage() from clicker thread wakes us instantly.
      // 1 ms cap keeps the UI responsive while still yielding the CPU.
      ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
    }
  }

private:
  //  Win32 window  â€” POINT 2: resizable + maximizable for large screens
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  bool createWindow() {
    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = ::GetModuleHandleA(nullptr);
    wc.lpszClassName = "VCS_MAIN_V1";
    wc.hIcon         = ::LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor       = ::LoadCursor(nullptr, IDC_ARROW);
    ::RegisterClassExA(&wc);

    int scr_w = ::GetSystemMetrics(SM_CXSCREEN);
    int scr_h = ::GetSystemMetrics(SM_CYSCREEN);
    // Center the 90%-sized window on the screen
    int pos_x = (scr_w - width_)  / 2;
    int pos_y = (scr_h - height_) / 2;

    // WS_OVERLAPPEDWINDOW â€” title bar with minimize/maximize/close + resize grip.
    // Operator can freely maximize to fill the 75"/85" smartboard at any time.
    DWORD wstyle = WS_OVERLAPPEDWINDOW;

    hwnd_ = ::CreateWindowExA(
        0, "VCS_MAIN_V1", hwnd_title_.c_str(), wstyle,
        pos_x, pos_y, width_, height_,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd_)
      return false;

    state_.hwnd_to_wake.store(reinterpret_cast<uintptr_t>(hwnd_),
                              std::memory_order_relaxed);
    ::SetWindowLongPtrA(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Start in normal restored mode â€” the operator can maximize via the title bar
    // maximize button or Windows snap.  WS_OVERLAPPEDWINDOW already provides
    // the resize grip + maximize button for 75"/85" touch screens.
    ::ShowWindow(hwnd_, SW_SHOWNORMAL);
    ::UpdateWindow(hwnd_);
    return true;
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  D3D11  (unchanged)
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  bool createDevice() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount          = 2;
    sd.BufferDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage          = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow         = hwnd_;
    sd.SampleDesc.Count     = 1;
    sd.Windowed             = TRUE;
    sd.SwapEffect           = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    UINT flags = 0;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = ::D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
        D3D11_SDK_VERSION, &sd, &swap_chain_, &device_, &fl, &device_ctx_);
    if (FAILED(hr)) return false;
    createRenderTarget();
    return true;
  }

  void createRenderTarget() {
    ID3D11Texture2D *back = nullptr;
    swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back));
    device_->CreateRenderTargetView(back, nullptr, &rtv_);
    back->Release();
  }
  void cleanRenderTarget() {
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  ImGui init + style
  //  POINT 1: All font sizes doubled for large-screen readability.
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename   = nullptr;

    applyStyle();
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, device_ctx_);

    // Font loading priority:
    //   1. Poppins-Regular.ttf  next to the executable
    //   2. Poppins-Regular.ttf  in Windows Fonts folder
    //   3. Segoe UI             Windows system font
    //   4. ImGui built-in fallback
    char exe_buf[MAX_PATH];
    ::GetModuleFileNameA(nullptr, exe_buf, MAX_PATH);
    std::string exe_dir(exe_buf);
    auto slash = exe_dir.find_last_of("\\/");
    if (slash != std::string::npos) exe_dir = exe_dir.substr(0, slash);

    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH  = true;

    const char *fontPaths[] = {
        (exe_dir + "\\Poppins-Regular.ttf").c_str(),
        "C:\\Windows\\Fonts\\Poppins-Regular.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
    };
    bool loaded = false;
    for (auto &fp : fontPaths) {
      if (fp && std::filesystem::exists(fp)) {
        // POINT 1: doubled from 18/24/14/20 â†’ 36/48/28/40
        font_regular_ = io.Fonts->AddFontFromFileTTF(fp, kFontRegular, &cfg);
        font_large_   = io.Fonts->AddFontFromFileTTF(fp, kFontLarge,   &cfg);
        font_small_   = io.Fonts->AddFontFromFileTTF(fp, kFontSmall,   &cfg);
        font_table_   = io.Fonts->AddFontFromFileTTF(fp, kFontTable,   &cfg);
        loaded = true;
        break;
      }
    }
    if (!loaded) {
      font_regular_ = io.Fonts->AddFontDefault();
      font_large_ = font_regular_;
      font_small_ = font_regular_;
      font_table_ = font_regular_;
    }
    io.Fonts->Build();
  }

  void applyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle &s = ImGui::GetStyle();

    // â”€â”€ Geometry â€” scaled up for touch targets â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    s.WindowRounding    = 12.f;
    s.ChildRounding     = 10.f;
    s.FrameRounding     = 8.f;
    s.GrabRounding      = 8.f;
    s.PopupRounding     = 10.f;
    s.ScrollbarRounding = 10.f;
    s.TabRounding       = 8.f;
    s.WindowBorderSize  = 0.f;
    s.ChildBorderSize   = 1.f;
    s.PopupBorderSize   = 1.f;
    s.FrameBorderSize   = 0.f;
    // POINT 1: Larger frame padding â€” more breathing room around text
    s.FramePadding      = ImVec2(18.f, 14.f);
    s.ItemSpacing       = ImVec2(16.f, 14.f);
    s.ItemInnerSpacing  = ImVec2(12.f, 10.f);
    s.ScrollbarSize     = 18.f;
    s.GrabMinSize       = 14.f;
    s.WindowPadding     = ImVec2(24.f, 24.f);
    s.IndentSpacing     = 28.f;

    // â”€â”€ Colours (identical to original) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    auto *c = s.Colors;
    c[ImGuiCol_WindowBg]            = kBg0;
    c[ImGuiCol_ChildBg]             = kBg1;
    c[ImGuiCol_PopupBg]             = {kBg1.x, kBg1.y, kBg1.z, 0.97f};
    c[ImGuiCol_Border]              = kBorder;
    c[ImGuiCol_BorderShadow]        = {0.f, 0.f, 0.f, 0.f};
    c[ImGuiCol_FrameBg]             = kBg2;
    c[ImGuiCol_FrameBgHovered]      = kBg3;
    c[ImGuiCol_FrameBgActive]       = {kOrange.x, kOrange.y, kOrange.z, 0.25f};
    c[ImGuiCol_TitleBg]             = kBg0;
    c[ImGuiCol_TitleBgActive]       = kBg0;
    c[ImGuiCol_TitleBgCollapsed]    = kBg0;
    c[ImGuiCol_MenuBarBg]           = kBg1;
    c[ImGuiCol_ScrollbarBg]         = kBg1;
    c[ImGuiCol_ScrollbarGrab]       = kBorder;
    c[ImGuiCol_ScrollbarGrabHovered]= kBg3;
    c[ImGuiCol_ScrollbarGrabActive] = kOrange;
    c[ImGuiCol_CheckMark]           = kOrange;
    c[ImGuiCol_SliderGrab]          = kOrange;
    c[ImGuiCol_SliderGrabActive]    = kOrangeAct;
    c[ImGuiCol_Button]              = kOrange;
    c[ImGuiCol_ButtonHovered]       = kOrangeHov;
    c[ImGuiCol_ButtonActive]        = kOrangeAct;
    c[ImGuiCol_Header]              = {kOrange.x, kOrange.y, kOrange.z, 0.30f};
    c[ImGuiCol_HeaderHovered]       = {kOrange.x, kOrange.y, kOrange.z, 0.55f};
    c[ImGuiCol_HeaderActive]        = {kOrange.x, kOrange.y, kOrange.z, 0.80f};
    c[ImGuiCol_Separator]           = kBorder;
    c[ImGuiCol_SeparatorHovered]    = kOrangeHov;
    c[ImGuiCol_SeparatorActive]     = kOrangeAct;
    c[ImGuiCol_ResizeGrip]          = {kOrange.x, kOrange.y, kOrange.z, 0.25f};
    c[ImGuiCol_ResizeGripHovered]   = {kOrange.x, kOrange.y, kOrange.z, 0.65f};
    c[ImGuiCol_ResizeGripActive]    = kOrangeAct;
    c[ImGuiCol_Tab]                 = kBg1;
    c[ImGuiCol_TabHovered]          = {kOrange.x, kOrange.y, kOrange.z, 0.65f};
    c[ImGuiCol_TabActive]           = kOrange;
    c[ImGuiCol_TabUnfocused]        = kBg0;
    c[ImGuiCol_TabUnfocusedActive]  = kBg2;
    c[ImGuiCol_TableBorderStrong]   = kBorder;
    c[ImGuiCol_TableBorderLight]    = {kBorder.x, kBorder.y, kBorder.z, 0.45f};
    c[ImGuiCol_TableRowBg]          = {0.f, 0.f, 0.f, 0.f};
    c[ImGuiCol_TableRowBgAlt]       = kBg1;
    c[ImGuiCol_TableHeaderBg]       = {0.10f, 0.13f, 0.18f, 1.f};
    c[ImGuiCol_PlotHistogram]       = kOrange;
    c[ImGuiCol_PlotHistogramHovered]= kOrangeHov;
    c[ImGuiCol_Text]                = kTextPrimary;
    c[ImGuiCol_TextDisabled]        = kTextDisabled;
    c[ImGuiCol_NavHighlight]        = kOrange;
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  Latency stamping â€” LOGIC UNCHANGED
  //  Stamps ui_delay_ms for every new row that hasn't been painted yet.
  //  Runs at the TOP of render() â€” even when the window is minimized.
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void stampPendingRows() {
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    struct Stamp { uint64_t serial; int64_t ts; };
    std::vector<Stamp> to_log;
    {
      std::lock_guard lk(state_.rows_mu);
      for (auto &row : state_.rows) {
        if (row.ui_delay_ms == -1) {
          row.ui_delay_ms = static_cast<int>(now_ms - row.entry_ts_ms);
          if (row.ui_delay_ms < 0) row.ui_delay_ms = 0;
          to_log.push_back({row.serial_no, now_ms});
        }
      }
    }
    for (auto &s : to_log)
      latencyLog().recordUiRender(s.serial, s.ts);
  }

  void render() {
    stampPendingRows(); // latency logging â€” must run before any ImGui calls
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    drawUI();
    ImGui::Render();
    static const float clear[4] = {kBg0.x, kBg0.y, kBg0.z, 1.f};
    device_ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    device_ctx_->ClearRenderTargetView(rtv_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swap_chain_->Present(0, 0); // vsync OFF â€” maximum throughput
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  Top-level layout
  //  POINT 2: ImGui window is always pinned to full DisplaySize, so when the
  //  Win32 window is maximized or resized the layout fills the new size
  //  automatically â€” no fixed pixel dimensions anywhere in the layout.
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void drawUI() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    drawHeader();
    drawContentArea();

    ImGui::End();

    if (show_register_panel_)
      drawRegisterPanel();
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  Header â€” gradient banner
  //  POINT 2: Height is 8% of display height so it scales with screen size.
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void drawHeader() {
    ImGuiIO &io  = ImGui::GetIO();
    // Scale header height: 8% of screen â€” looks good on both HD and 4K
    const float HEADER_H = std::max(80.f, io.DisplaySize.y * 0.08f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBg0);
    ImGui::BeginChild("##hdr", ImVec2(0.f, HEADER_H), false,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Gradient: dark-navy â†’ orange (left-to-right)
    ImVec2 wp = ImGui::GetWindowPos();
    float  w  = ImGui::GetWindowWidth();
    auto  *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(
        {wp.x, wp.y}, {wp.x + w, wp.y + HEADER_H},
        IM_COL32(13, 17, 23, 255),
        IM_COL32(255, 107, 0, 200),
        IM_COL32(255, 107, 0, 200),
        IM_COL32(13, 17, 23, 255));
    // Bottom accent line
    dl->AddLine({wp.x, wp.y + HEADER_H - 1.f},
                {wp.x + w, wp.y + HEADER_H - 1.f},
                IM_COL32(255, 107, 0, 180), 2.f);

    // Title â€” large font
    if (font_large_) ImGui::PushFont(font_large_);
    float title_y = (HEADER_H - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPos({24.f, title_y});
    ImGui::TextColored({1.f, 1.f, 1.f, 1.f}, "VEDANTU CLICKER SYSTEM");
    if (font_large_) ImGui::PopFont();

    // Status chips â€” right-aligned
    bool dongle_ok  = state_.dongle_connected.load();
    bool session_ok = state_.session_valid.load();

    const char *dongle_label  = dongle_ok  ? "Dongle: CONNECTED"    : "Dongle: DISCONNECTED";
    const char *session_label = session_ok ? "Session: ACTIVE"      : "Session: PENDING";
    ImVec4 dongle_col  = dongle_ok  ? kGreen : kRed;
    ImVec4 session_col = session_ok ? kGreen : kAmber;

    if (font_small_) ImGui::PushFont(font_small_);
    float chip_y    = (HEADER_H - ImGui::GetTextLineHeightWithSpacing()) * 0.5f;
    float right_edge = ImGui::GetWindowWidth() - 20.f;

    auto drawChip = [&](const char *label, ImVec4 col) {
      ImVec2 ts = ImGui::CalcTextSize(label);
      float cx  = right_edge - ts.x - 24.f;
      ImGui::SetCursorPos({cx - 12.f, chip_y - 6.f});
      ImVec2 cpos = ImGui::GetCursorScreenPos();
      dl->AddRectFilled(cpos, {cpos.x + ts.x + 28.f, cpos.y + ts.y + 14.f},
                        ImGui::ColorConvertFloat4ToU32({col.x, col.y, col.z, 0.22f}), 14.f);
      dl->AddRect(cpos, {cpos.x + ts.x + 28.f, cpos.y + ts.y + 14.f},
                  ImGui::ColorConvertFloat4ToU32(col), 14.f, 0, 1.5f);
      ImGui::SetCursorPos({cx, chip_y});
      ImGui::TextColored(col, "%s", label);
      right_edge = cx - 16.f;
    };

    drawChip(session_label, session_col);
    drawChip(dongle_label,  dongle_col);

    if (font_small_) ImGui::PopFont();
    ImGui::EndChild();
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  Content area â€” padded container for everything below the header
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void drawContentArea() {
    ImGuiIO &io = ImGui::GetIO();
    // Uniform padding: 2% of width horizontally, 1.5% of height vertically
    float px = std::clamp(io.DisplaySize.x * 0.02f, 20.f, 48.f);
    float py = std::clamp(io.DisplaySize.y * 0.015f, 14.f, 32.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px, py));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBg0);
    ImGui::BeginChild("##content", ImVec2(0.f, 0.f), false,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    drawToolbar();
    ImGui::Spacing();
    drawSessionPanel();
    ImGui::Spacing();
    drawEventTable();

    ImGui::EndChild();
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  Toolbar â€” Pair / Stop button
  //  POINT 2: Button width is 16% of content width so it scales with screen.
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void drawToolbar() {
    float avail_w = ImGui::GetContentRegionAvail().x;
    // Button width: 16% of available width, min 200, max 380
    float BTN_W = std::clamp(avail_w * 0.16f, 200.f, 380.f);
    float BTN_H = 64.f; // tall touch target

    float offset = avail_w - BTN_W;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

    bool reg_active = state_.pairing_active.load();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 22.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(20.f, 12.f));

    if (reg_active) {
      ImGui::PushStyleColor(ImGuiCol_Button,        kRed);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.95f, 0.25f, 0.25f, 1.f});
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.75f, 0.10f, 0.10f, 1.f});
      ImGui::PushStyleColor(ImGuiCol_Text,          {1.f, 1.f, 1.f, 1.f});
      if (font_regular_) ImGui::PushFont(font_regular_);
      if (ImGui::Button("Stop Pairing", ImVec2(BTN_W, BTN_H))) {
        show_register_panel_ = false;
        if (on_reg_toggle_) on_reg_toggle_(false);
      }
      if (font_regular_) ImGui::PopFont();
      ImGui::PopStyleColor(4);
    } else {
      ImGui::PushStyleColor(ImGuiCol_Button,        kOrange);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kOrangeHov);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kOrangeAct);
      ImGui::PushStyleColor(ImGuiCol_Text,          {1.f, 1.f, 1.f, 1.f});
      if (font_regular_) ImGui::PushFont(font_regular_);
      if (ImGui::Button("Pair Clickers", ImVec2(BTN_W, BTN_H))) {
        show_register_panel_ = true;
        if (on_reg_toggle_) on_reg_toggle_(true);
      }
      if (font_regular_) ImGui::PopFont();
      ImGui::PopStyleColor(4);
    }
    ImGui::PopStyleVar(2);
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  Session Panel
  //  Row 1: [Session ID input â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€] [paste] [VALIDATE]
  //  Row 2: [Title chip] [Presenter chip] [Time chip]  <- only after validation
  //
  //  Card height is adaptive: compact before validation, expands to show
  //  info chips after a successful validate.
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void drawSessionPanel() {
    ImGuiIO &io     = ImGui::GetIO();
    bool session_ok = state_.session_valid.load();
    float card_w    = ImGui::GetContentRegionAvail().x;
    // Adaptive: compact before validation, taller when chips shown
    float card_h = session_ok
        ? std::clamp(io.DisplaySize.y * 0.19f, 185.f, 275.f)
        : std::clamp(io.DisplaySize.y * 0.12f, 120.f, 165.f);

    // Card shadow
    ImVec2 card_tl = ImGui::GetCursorScreenPos();
    auto  *dl      = ImGui::GetWindowDrawList();
    dl->AddRectFilled({card_tl.x + 4.f, card_tl.y + 5.f},
                      {card_tl.x + card_w + 4.f, card_tl.y + card_h + 5.f},
                      IM_COL32(0, 0, 0, 60), 12.f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBg1);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorderAcc);
    // Tight padding: 10 px â€” minimal wasted space inside the card
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
    ImGui::BeginChild("##session", ImVec2(card_w, card_h), true,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2); // ChildBorderSize + WindowPadding

    // Section label
    if (font_small_) ImGui::PushFont(font_small_);
    ImGui::TextColored(kOrange, "SESSION CONFIGURATION");
    if (font_small_) ImGui::PopFont();
    // No extra Spacing() â€” ItemSpacing already provides a small natural gap

    // â”€â”€ Button widths â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    float avail_w        = ImGui::GetContentRegionAvail().x;
    float btn_paste_w    = std::clamp(avail_w * 0.08f, 85.f, 110.f);
    float btn_validate_w = std::clamp(avail_w * 0.16f, 160.f, 220.f);
    float gap            = 10.f;
    float input_w        = avail_w - btn_paste_w - btn_validate_w - gap * 2.f;

    // â”€â”€ Session ID input â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        kBg2);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,  kBg3);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.f, 10.f));
    if (font_small_) ImGui::PushFont(font_small_);
    ImGui::SetNextItemWidth(input_w);
    ImGui::InputText("##sid", session_id_buf_, sizeof(session_id_buf_));
    if (font_small_) ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // Placeholder hint (shown when field is empty)
    if (session_id_buf_[0] == '\0') {
      ImVec2 ip = ImGui::GetItemRectMin();
      ip.x += 14.f; ip.y += 12.f;
      dl->AddText(nullptr, 0.f, ip,
                  ImGui::ColorConvertFloat4ToU32(kTextDisabled),
                  "Tap PASTE or type Session ID...");
    }

    // â”€â”€ PASTE button â€” small chip style â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Uses font_small so it sits comfortably beside the larger input text.
    ImGui::SameLine(0.f, gap);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(8.f, 10.f));
    ImGui::PushStyleColor(ImGuiCol_Button,        kBg2);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBg3);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {kOrange.x, kOrange.y, kOrange.z, 0.40f});
    ImGui::PushStyleColor(ImGuiCol_Text,          kOrange);
    if (font_small_) ImGui::PushFont(font_small_);
    if (ImGui::Button("PASTE", ImVec2(btn_paste_w, 0.f))) {
      if (::OpenClipboard(nullptr)) {
        HANDLE h = ::GetClipboardData(CF_TEXT);
        if (h) {
          const char *txt = static_cast<const char *>(::GlobalLock(h));
          if (txt) strncpy_s(session_id_buf_, sizeof(session_id_buf_), txt, _TRUNCATE);
          ::GlobalUnlock(h);
        }
        ::CloseClipboard();
      }
      lp_pasted_ = true;
    }
    if (font_small_) ImGui::PopFont();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);

    // â”€â”€ VALIDATE button â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    ImGui::SameLine(0.f, gap);
    bool validating = validating_.load();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
    // Same padding as PASTE â€” uniform height across all three controls
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(8.f, 10.f));
    if (font_small_) ImGui::PushFont(font_small_);

    if (validating) {
      ImGui::BeginDisabled();
      float t    = static_cast<float>(ImGui::GetTime());
      char  spin[32];
      int   dots = (static_cast<int>(t * 3.0) % 3) + 1;
      std::snprintf(spin, sizeof(spin), "Validating%.*s", dots, "...");
      ImGui::Button(spin, ImVec2(btn_validate_w, 0.f));
      ImGui::EndDisabled();
    } else {
      ImGui::PushStyleColor(ImGuiCol_Button,        kOrange);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kOrangeHov);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kOrangeAct);
      ImGui::PushStyleColor(ImGuiCol_Text,          {1.f, 1.f, 1.f, 1.f});
      if (ImGui::Button("VALIDATE", ImVec2(btn_validate_w, 0.f))) {
        std::string sid(session_id_buf_);
        if (!sid.empty() && on_validate_ && !validating_.load()) {
          validating_ = true;
          if (validate_thread_.joinable()) validate_thread_.join();
          validate_thread_ = std::thread([this, sid] {
            try {
              on_validate_(sid);
            } catch (const std::exception &e) {
              Logger::errors()->error("[UI] Validation thread exception: {}", e.what());
            } catch (...) {
              Logger::errors()->error("[UI] Validation thread unknown exception");
            }
            validating_ = false;
          });
        }
      }
      ImGui::PopStyleColor(4);
    }

    if (font_small_) ImGui::PopFont();
    ImGui::PopStyleVar(2);

    // â”€â”€ Session info chips (shown after successful validation) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Each chip: small rounded box with a dim label + accented value.
    if (session_ok) {
      std::lock_guard lk(state_.session_mu);
      ImGui::Spacing();

      // Lambda: draws one chip at current cursor, advances cursor via Dummy.
      auto draw_chip = [&](const char *label, const std::string &value,
                           ImVec4 accent) {
        if (value.empty()) return;
        if (font_small_) ImGui::PushFont(font_small_);
        ImVec2 lsz   = ImGui::CalcTextSize(label);
        ImVec2 vsz   = ImGui::CalcTextSize(value.c_str());
        float  px    = 10.f, py = 6.f, glv = 5.f;
        float  cw    = px + lsz.x + glv + vsz.x + px;
        float  ch    = std::max(lsz.y, vsz.y) + py * 2.f;
        ImVec2 p     = ImGui::GetCursorScreenPos();
        // Background + border
        dl->AddRectFilled(p, {p.x + cw, p.y + ch}, IM_COL32(20,28,42,230), 7.f);
        dl->AddRect(p, {p.x + cw, p.y + ch},
                    ImGui::ColorConvertFloat4ToU32({accent.x,accent.y,accent.z,0.65f}),
                    7.f, 0, 1.5f);
        // Label (dim)
        dl->AddText(font_small_, kFontSmall, {p.x+px, p.y+py},
                    ImGui::ColorConvertFloat4ToU32(kTextSecondary), label);
        // Value (accented)
        dl->AddText(font_small_, kFontSmall, {p.x+px+lsz.x+glv, p.y+py},
                    ImGui::ColorConvertFloat4ToU32(accent), value.c_str());
        ImGui::Dummy(ImVec2(cw, ch));
        ImGui::SameLine(0.f, 10.f);
        if (font_small_) ImGui::PopFont();
      };

      draw_chip("Title: ",     state_.session_title,     kGreen);
      draw_chip("Presenter: ", state_.session_presenter,  kTextPrimary);
      std::string t_str = state_.session_start + "  ->  " + state_.session_end;
      draw_chip("Time: ",      t_str,                     kOrange);
      ImGui::NewLine();
    }

    // ── Inline validation error banner ──────────────────────────────────
    // Shown instead of the old blocking MessageBoxA popup.
    // Auto-clears when the operator successfully validates a session.
    if (state_.validation_error.load()) {
      ImGui::Spacing();
      ImVec2 banner_pos = ImGui::GetCursorScreenPos();
      float  banner_w   = ImGui::GetContentRegionAvail().x;
      float  banner_h   = 42.f;
      dl->AddRectFilled(banner_pos, {banner_pos.x + banner_w, banner_pos.y + banner_h},
                        IM_COL32(180, 30, 30, 220), 6.f);
      dl->AddRect(banner_pos, {banner_pos.x + banner_w, banner_pos.y + banner_h},
                  IM_COL32(240, 60, 60, 255), 6.f, 0, 1.5f);
      if (font_small_) ImGui::PushFont(font_small_);
      ImGui::SetCursorScreenPos({banner_pos.x + 14.f, banner_pos.y + (banner_h - ImGui::GetTextLineHeight()) * 0.5f});
      ImGui::TextColored({1.f, 0.85f, 0.85f, 1.f},
          "\u26a0  Session validation failed — check the Session ID and try again.");
      ImGui::SameLine(banner_w - 80.f);
      ImGui::PushStyleColor(ImGuiCol_Button,        {0.f, 0.f, 0.f, 0.f});
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {1.f, 1.f, 1.f, 0.15f});
      ImGui::PushStyleColor(ImGuiCol_Text,          {1.f, 0.85f, 0.85f, 1.f});
      if (ImGui::Button("Dismiss##valerr"))
        state_.validation_error.store(false);
      ImGui::PopStyleColor(3);
      if (font_small_) ImGui::PopFont();
      ImGui::Dummy({0.f, banner_h}); // advance cursor past banner
    }

    ImGui::EndChild();
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  Event Table
  //  POINT 1: 4 columns only â€” No. | Device ID | Value | Entry Time
  //           UI Delay and API Delay columns removed from display.
  //           (Latency tracking logic in stampPendingRows / updateEventLatency
  //            is completely unchanged â€” CSV logging still works.)
  //  POINT 2: Column widths are percentages of available width so the table
  //           fills the screen at any resolution or orientation.
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void drawEventTable() {
    // Section header
    if (font_regular_) ImGui::PushFont(font_regular_);
    ImGui::TextColored(kOrange, "LIVE CLICK EVENTS");
    ImGui::SameLine();
    {
      std::lock_guard lk(state_.rows_mu);
      if (font_small_) { ImGui::PopFont(); ImGui::PushFont(font_small_); }
      ImGui::TextColored(kTextDisabled, "  %zu event(s)", state_.rows.size());
      if (font_small_) { ImGui::PopFont(); }
      else             { ImGui::PopFont(); }
    }
    // Restore regular font if it was left on stack
    // (PopFont balance handled inside the block above)
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg,  kBg1);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_Border,   kBorder);

    static ImGuiTableFlags tflags =
        ImGuiTableFlags_ScrollY        |
        ImGuiTableFlags_RowBg          |
        ImGuiTableFlags_BordersOuter   |
        ImGuiTableFlags_BordersInnerV  |
        ImGuiTableFlags_SizingFixedFit;

    ImVec2 table_size(0.f, ImGui::GetContentRegionAvail().y - 4.f);

    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg,     {0.10f, 0.13f, 0.18f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, kBorder);
    ImGui::PushStyleColor(ImGuiCol_TableRowBg,        {0.f, 0.f, 0.f, 0.f});
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt,     kBg1);

    // Even column widths â€” balanced proportions across any screen width
    float avail_w  = ImGui::GetContentRegionAvail().x;
    // No.  =  8%  (wider than before â€” serial numbers are clearly readable)
    // Dev  = 38%  (device IDs are the longest content)
    // Val  =  8%  (single digit value A-F)
    // Time = 46%  stretch (fills remaining space evenly)
    float col_no   = std::max(80.f,  avail_w * 0.08f);
    float col_dev  = std::max(220.f, avail_w * 0.38f);
    float col_val  = std::max(80.f,  avail_w * 0.08f);
    // Entry Time: WidthStretch fills the rest

    // 4 columns â€” No., Device ID, Value, Entry Time
    if (ImGui::BeginTable("##events", 4, tflags, table_size)) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("No.",        ImGuiTableColumnFlags_WidthFixed,   col_no);
      ImGui::TableSetupColumn("Device ID",  ImGuiTableColumnFlags_WidthFixed,   col_dev);
      ImGui::TableSetupColumn("Value",      ImGuiTableColumnFlags_WidthFixed,   col_val);
      ImGui::TableSetupColumn("Entry Time", ImGuiTableColumnFlags_WidthStretch);

      // Custom header with orange text
      ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
      const char *headers[] = {"No.", "Device ID", "Value", "Entry Time"};
      for (int col = 0; col < 4; ++col) {
        ImGui::TableSetColumnIndex(col);
        if (font_small_) ImGui::PushFont(font_small_);
        ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
        ImGui::TableHeader(headers[col]);
        ImGui::PopStyleColor();
        if (font_small_) ImGui::PopFont();
      }

      // Rows â€” most-recent first
      {
        std::lock_guard lk(state_.rows_mu);
        bool is_first = true;

        for (auto &row : state_.rows) {
          ImGui::TableNextRow();

          // Highlight the newest row with a subtle orange tint
          if (is_first) {
            for (int c = 0; c < 4; ++c) {
              ImGui::TableSetColumnIndex(c);
              ImVec2 rmin = ImGui::GetItemRectMin();
              ImVec2 rmax = ImGui::GetItemRectMax();
              ImGui::GetWindowDrawList()->AddRectFilled(
                  {rmin.x, ImGui::GetCursorScreenPos().y - 1.f},
                  {rmax.x + 999.f,
                   ImGui::GetCursorScreenPos().y +
                       ImGui::GetTextLineHeightWithSpacing() + 1.f},
                  IM_COL32(255, 107, 0, 22));
            }
            is_first = false;
          }

          if (font_table_) ImGui::PushFont(font_table_);

          // No.
          ImGui::TableSetColumnIndex(0);
          ImGui::TextColored(kTextSecondary, "%llu", row.serial_no);

          // Device ID
          ImGui::TableSetColumnIndex(1);
          ImGui::PushID(static_cast<int>(row.serial_no));
          ImGui::Selectable(row.device_id.c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns);
          ImGui::PopID();

          // Value
          ImGui::TableSetColumnIndex(2);
          ImGui::TextColored(kOrange, "%d", row.value);

          // Entry Time
          ImGui::TableSetColumnIndex(3);
          ImGui::TextColored(kTextSecondary, "%s", row.timestamp.c_str());

          if (font_table_) ImGui::PopFont();
        }
      } // rows_mu released

      ImGui::EndTable();
    }

    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar();
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  Registration Panel â€” centered modal
  //  Scaled up: 50% Ã— 60% of display so it reads well on 75"/85" screens.
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void drawRegisterPanel() {
    ImGuiIO &io = ImGui::GetIO();
    // Panel: 50% wide, 60% tall â€” clipped to screen edges
    float PW = std::min(io.DisplaySize.x * 0.50f, io.DisplaySize.x - 40.f);
    float PH = std::min(io.DisplaySize.y * 0.60f, io.DisplaySize.y - 40.f);
    float cx = std::max(20.f, (io.DisplaySize.x - PW) * 0.5f);
    float cy = std::max(20.f, (io.DisplaySize.y - PH) * 0.5f);

    ImGui::SetNextWindowPos({cx, cy},    ImGuiCond_Always);
    ImGui::SetNextWindowSize({PW, PH},   ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg1);
    ImGui::PushStyleColor(ImGuiCol_Border,   kOrange);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(28.f, 24.f));

    ImGui::Begin("##regpanel", &show_register_panel_,
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoCollapse  |
        ImGuiWindowFlags_NoTitleBar);

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    // Header strip
    ImVec2 wpos = ImGui::GetWindowPos();
    auto  *dl   = ImGui::GetWindowDrawList();
    float HDR   = 56.f;
    dl->AddRectFilled(wpos, {wpos.x + PW, wpos.y + HDR},
                      IM_COL32(255, 107, 0, 220), 10.f, ImDrawFlags_RoundCornersTop);

    if (font_regular_) ImGui::PushFont(font_regular_);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.f);
    ImGui::TextColored({1.f, 1.f, 1.f, 1.f}, "CLICKER PAIRING");

    // Close button
    float close_x = PW - 52.f;
    ImGui::SameLine(close_x);
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.f, 0.f, 0.f, 0.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {1.f, 1.f, 1.f, 0.18f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {1.f, 1.f, 1.f, 0.35f});
    ImGui::PushStyleColor(ImGuiCol_Text,          {1.f, 1.f, 1.f, 1.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.f);
    if (ImGui::Button("X##cls", ImVec2(40.f, 40.f))) {
      show_register_panel_ = false;
      if (on_reg_toggle_) on_reg_toggle_(false);
      state_.pairing_active.store(false);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (font_regular_) ImGui::PopFont();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + HDR - 8.f);

    // Pairing status
    bool reg_active = state_.pairing_active.load();
    if (font_regular_) ImGui::PushFont(font_regular_);
    if (reg_active) {
      float t     = static_cast<float>(ImGui::GetTime());
      float alpha = 0.5f + 0.5f * sinf(t * 3.5f);
      ImGui::PushStyleColor(ImGuiCol_Text, {kGreen.x, kGreen.y, kGreen.z, alpha});
      ImGui::Text("  Listening... Power on each clicker now");
      ImGui::PopStyleColor();
    } else {
      ImGui::TextColored(kTextSecondary, "  Press 'Pair Clickers' to start pairing.");
    }
    if (font_regular_) ImGui::PopFont();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, kBorder);
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Registered clicker list
    {
      std::lock_guard lk(state_.reg_mu);
      if (state_.registered_clickers.empty()) {
        if (font_regular_) ImGui::PushFont(font_regular_);
        ImGui::TextColored(kTextDisabled, "  No clickers registered yet.");
        if (font_regular_) ImGui::PopFont();
      } else {
        if (font_small_) ImGui::PushFont(font_small_);
        ImGui::TextColored(kTextSecondary, "  Registered: %zu clicker(s)",
                           state_.registered_clickers.size());
        if (font_small_) ImGui::PopFont();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, kBg2);
        ImGui::PushStyleColor(ImGuiCol_Border,  kBorder);
        float list_h = PH * 0.40f;
        ImGui::BeginChild("##reglist", ImVec2(0.f, list_h), true);

        for (auto &rc : state_.registered_clickers) {
          if (font_regular_) ImGui::PushFont(font_regular_);
          ImGui::TextColored(kGreen, "  %s", rc.sn.c_str());
          ImGui::SameLine(PW * 0.40f);
          ImGui::TextColored(kTextSecondary, "Slot:%-4s  M:%s  FW:%s",
                             rc.key_id.c_str(), rc.model.c_str(), rc.version.c_str());
          if (font_regular_) ImGui::PopFont();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button,        kRed);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.95f, 0.25f, 0.25f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_Text,          {1.f, 1.f, 1.f, 1.f});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
        if (font_regular_) ImGui::PushFont(font_regular_);
        if (ImGui::Button("  Clear List  "))
          state_.clearRegistered();
        if (font_regular_) ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
      }
    }

    ImGui::End();
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  Cleanup (unchanged)
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void destroy() {
    if (validate_thread_.joinable())
      validate_thread_.join();
    if (device_ctx_)  ImGui_ImplDX11_Shutdown();
    if (hwnd_)        ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanRenderTarget();
    if (swap_chain_)  swap_chain_->Release();
    if (device_ctx_)  device_ctx_->Release();
    if (device_)      device_->Release();
    if (hwnd_) {
      ::DestroyWindow(hwnd_);
      ::UnregisterClassA("VCS_MAIN_V1", nullptr);
    }
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  WndProc â€” handles resize (including maximize) for D3D swap chain
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  static LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
      return true;

    auto *self = reinterpret_cast<MainWindow *>(
        ::GetWindowLongPtrA(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_SIZE:
      // POINT 2: Handles window maximize and free resize â€”
      // the D3D swap chain is resized to match, and ImGui's
      // DisplaySize is updated automatically by ImGui_ImplWin32_NewFrame().
      if (self && self->device_ && wParam != SIZE_MINIMIZED) {
        self->cleanRenderTarget();
        self->swap_chain_->ResizeBuffers(
            0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
        self->createRenderTarget();
      }
      return 0;
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
    }
    return ::DefWindowProcA(hWnd, msg, wParam, lParam);
  }

  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  Members
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  UiState          &state_;
  ValidateCallback  on_validate_;
  RegisterStartStop on_reg_toggle_;

  HWND                    hwnd_{nullptr};
  std::string             hwnd_title_;
  int                     width_{1280}, height_{720};
  ID3D11Device           *device_{nullptr};
  ID3D11DeviceContext    *device_ctx_{nullptr};
  IDXGISwapChain         *swap_chain_{nullptr};
  ID3D11RenderTargetView *rtv_{nullptr};

  char session_id_buf_[256]{};
  std::atomic<bool>  validating_{false};
  bool               show_register_panel_{false};

  // POINT 3: Long-press paste state
  float lp_start_time_{-1.f}; // ImGui time when press began; -1 = not active
  bool  lp_pasted_{false};    // true once paste has fired for this press

  ImFont *font_regular_{nullptr};
  ImFont *font_large_{nullptr};
  ImFont *font_small_{nullptr};
  ImFont *font_table_{nullptr};

  std::thread validate_thread_;
};

} // namespace svs
