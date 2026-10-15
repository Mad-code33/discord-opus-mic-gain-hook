#include <windows.h>
#include "hook.h"

static HMODULE g_self = nullptr;

static DWORD WINAPI Main(LPVOID) {
    if (InstallHook()) RunGui();   // gui крутится пока не закроют крестиком

    // вернуть оригинальные байты, даже если dll не выгрузится
    SetPatches(PATCH_STEREO, false);
    SetFilterless(false);   // сеттеры/APM — значения дискорда, патчи — оригинал
    const bool ok = UninstallHook();
    RestoreDiskStubs();   // хуки уже сняты (MH_DisableHook) — C3 на место
    if (ok) FreeLibraryAndExitThread(g_self, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_self = h;
        HANDLE th = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (th) CloseHandle(th);
    }
    return TRUE;
}
