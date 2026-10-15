#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <math.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "hook.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static ID3D11Device*           g_dev    = nullptr;
static ID3D11DeviceContext*    g_ctx    = nullptr;
static IDXGISwapChain*         g_swap   = nullptr;
static ID3D11RenderTargetView* g_rtv    = nullptr;

static void MakeRtv() {
    ID3D11Texture2D* bb = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (bb) { g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv); bb->Release(); }
}
static void DropRtv() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

static bool MakeDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL lvl[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got;
    if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, lvl, 1,
            D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx))) {
        return false;
    }
    MakeRtv();
    return true;
}
static void DropDevice() {
    DropRtv();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx)  { g_ctx->Release();  g_ctx = nullptr; }
    if (g_dev)  { g_dev->Release();  g_dev = nullptr; }
}

static LRESULT WINAPI WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // верхняя полоса тащит окно
    if (m == WM_NCHITTEST) {
        LRESULT hit = DefWindowProc(h, m, w, l);
        if (hit != HTCLIENT) return hit;
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(h, &p);
        if (p.y >= 0 && p.y < 40) return HTCAPTION;
        return HTCLIENT;
    }
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    switch (m) {
    case WM_SIZE:
        if (g_dev && w != SIZE_MINIMIZED) {
            DropRtv();
            g_swap->ResizeBuffers(0, LOWORD(l), HIWORD(l), DXGI_FORMAT_UNKNOWN, 0);
            MakeRtv();
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static void Style() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding   = ImVec2(18, 14);
    s.FramePadding    = ImVec2(10, 7);
    s.ItemSpacing     = ImVec2(10, 9);
    s.WindowRounding  = 10.0f;
    s.FrameRounding   = 7.0f;
    s.GrabRounding    = 7.0f;
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize  = 0.0f;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.015f, 0.015f, 0.018f, 1.0f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.075f, 0.077f, 0.090f, 1.0f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.105f, 0.110f, 0.130f, 1.0f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.130f, 0.140f, 0.165f, 1.0f);
    c[ImGuiCol_Button]           = ImVec4(0.100f, 0.105f, 0.120f, 1.0f);
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.165f, 0.180f, 0.205f, 1.0f);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.215f, 0.235f, 0.265f, 1.0f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.25f, 0.72f, 0.54f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.34f, 0.88f, 0.66f, 1.0f);
    c[ImGuiCol_Text]             = ImVec4(0.91f, 0.92f, 0.94f, 1.0f);
    c[ImGuiCol_TextDisabled]     = ImVec4(0.48f, 0.50f, 0.55f, 1.0f);
}

// прилепить окно к правому-верхнему углу дискорда
static void PinToDiscord(HWND self, int w, int h) {
    struct C { HWND self; DWORD pid; HWND best; long area; }
    c{ self, GetCurrentProcessId(), nullptr, 0 };
    EnumWindows([](HWND hh, LPARAM lp) -> BOOL {
        C* c = (C*)lp;
        if (hh == c->self) return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(hh, &pid);
        if (pid != c->pid || !IsWindowVisible(hh) || GetWindow(hh, GW_OWNER)) return TRUE;
        RECT r; if (!GetWindowRect(hh, &r)) return TRUE;
        long a = (r.right - r.left) * (long)(r.bottom - r.top);
        if (a > c->area) { c->area = a; c->best = hh; }
        return TRUE;
    }, (LPARAM)&c);

    int x = 120, y = 120;
    RECT r;
    if (c.best && GetWindowRect(c.best, &r) &&
        (r.right - r.left) > w && (r.bottom - r.top) > h) {
        x = r.right - w - 24; y = r.top + 24;
    }
    SetWindowPos(self, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

void RunGui() {
    constexpr int W = 380, H = 292;

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0,
                       GetModuleHandle(nullptr), nullptr, nullptr,
                       nullptr, nullptr, L"FreeHookCls", nullptr };
    RegisterClassExW(&wc);

    // topmost + toolwindow => висит над дискордом, нет в панели задач / alt-tab
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName,
        L"FreeHook by madect", WS_POPUP, 120, 120, W, H,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { UnregisterClassW(wc.lpszClassName, wc.hInstance); return; }
    SetLayeredWindowAttributes(hwnd, 0, 238, LWA_ALPHA);

    if (!MakeDevice(hwnd)) {
        DropDevice(); DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance); return;
    }

    PinToDiscord(hwnd, W, H);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    Style();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    bool done = false, visible = true, insDown = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // insert — скрыть/показать; ловим фронт нажатия: бит &1 общий на все
        // процессы и его "съедает" любой другой GetAsyncKeyState
        const bool ins = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        const bool insPressed = ins && !insDown;
        insDown = ins;
        if (insPressed) {
            visible = !visible;
            if (visible) { PinToDiscord(hwnd, W, H); ShowWindow(hwnd, SW_SHOW); }
            else ShowWindow(hwnd, SW_HIDE);
        }
        if (!visible) { Sleep(16); continue; }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##main", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.34f, 0.88f, 0.66f, 1.0f));
        ImGui::Text("FreeHook");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("mic gain  |  Insert");

        ImGui::SetCursorPos(ImVec2(io.DisplaySize.x - 42.0f, 9.0f));
        if (ImGui::Button("X", ImVec2(28, 24))) done = true;   // выгрузить dll

        ImGui::Separator();
        ImGui::Spacing();

        float gain = g_gain;
        float dB = 20.0f * log10f(gain > 0.0001f ? gain : 0.0001f);
        ImGui::Text("Gain");
        ImGui::SameLine();
        ImGui::TextDisabled("%.2fx  (%+.1f dB)", gain, dB);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##gain", &gain, 1.0f, 16.0f, "%.2fx",
                               ImGuiSliderFlags_Logarithmic)) {
            g_gain = gain;
        }

        const float bw = (ImGui::GetContentRegionAvail().x - 40.0f) / 5.0f;
        if (ImGui::Button("1x",  ImVec2(bw, 0))) g_gain = 1.0f;  ImGui::SameLine();
        if (ImGui::Button("2x",  ImVec2(bw, 0))) g_gain = 2.0f;  ImGui::SameLine();
        if (ImGui::Button("4x",  ImVec2(bw, 0))) g_gain = 4.0f;  ImGui::SameLine();
        if (ImGui::Button("8x",  ImVec2(bw, 0))) g_gain = 8.0f;  ImGui::SameLine();
        if (ImGui::Button("16x", ImVec2(bw, 0))) g_gain = 16.0f;

        ImGui::Spacing();
        bool fl = g_filterless;
        if (ImGui::Checkbox("Filterless (raw audio)", &fl)) SetFilterless(fl);
        int stuck, seen, on, disk, na;
        bool apm, attn;
        FilterCounts(stuck, seen, apm, attn);
        PatchCounts(PATCH_FILTERLESS, on, disk, na);
        ImGui::SetItemTooltip("NS, NC, AGC, echo, AEC, sidechain, ducking off\n"
                              "OS processing bypassed, no sample-rate cap\n"
                              "WebRTC APM config: %s\n"
                              "attenuation: %s\n"
                              "high-pass / opus AUDIO: %d on  %d disk  %d n/a\n"
                              "opus AUDIO and attenuation: rejoin voice",
                              apm ? "all stages off" : "n/a", attn ? "off" : "n/a",
                              on, disk, na);
        ImGui::SameLine();
        if (stuck) {
            ImGui::TextDisabled("pre-patched on disk");
            ImGui::SetItemTooltip("%d setters are 'ret' in discord_voice.node\n"
                                  "and can't be restored: Filterless skips them", stuck);
        } else {
            ImGui::TextDisabled("NS/AGC/HPF/duck off");
        }
        // дискорд ещё не звал ни один сеттер — переотправить нечем
        if (g_filterless && !seen) ImGui::TextDisabled("rejoin voice / toggle a voice setting once");

        static bool stereo = false;
        if (ImGui::Checkbox("Stereo / HQ", &stereo)) {
            SetPatches(PATCH_STEREO, stereo);
            g_hq = stereo;
        }
        ImGui::SetItemTooltip("stereo SDP, 48k (in memory)\n"
                              "opus complexity 10, fullband, no DTX: %d/3 hooked\n"
                              "rejoin voice to apply", HqHooks());
        PatchCounts(PATCH_STEREO, on, disk, na);
        ImGui::SameLine();
        ImGui::TextDisabled("%d on  %d disk  %d n/a", on, disk, na);

        ImGui::SetCursorPosY(io.DisplaySize.y - 26.0f);
        ImGui::TextDisabled("created by madect");

        ImGui::End();

        ImGui::Render();
        const float clear[4] = { 0.015f, 0.015f, 0.018f, 1.0f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DropDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
