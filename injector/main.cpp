#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

// ищем процесс Discord, в котором загружен discord_voice.node (т.е. ты в войсе)
static DWORD FindVoicePid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    DWORD found = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            // Discord.exe / DiscordPTB.exe / DiscordCanary.exe
            if (_wcsnicmp(pe.szExeFile, L"Discord", 7) != 0) continue;
            HANDLE ms = CreateToolhelp32Snapshot(
                TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pe.th32ProcessID);
            if (ms == INVALID_HANDLE_VALUE) continue;
            MODULEENTRY32W me = {}; me.dwSize = sizeof(me);
            if (Module32FirstW(ms, &me)) {
                do {
                    if (_wcsicmp(me.szModule, L"discord_voice.node") == 0) {
                        found = pe.th32ProcessID; break;
                    }
                } while (Module32NextW(ms, &me));
            }
            CloseHandle(ms);
            if (found) break;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

int wmain(int argc, wchar_t** argv) {
    // путь к dll: аргумент либо hookdll.dll рядом с инжектором
    wchar_t dll[MAX_PATH];
    if (argc >= 2) {
        wcscpy_s(dll, MAX_PATH, argv[1]);
    } else {
        GetModuleFileNameW(nullptr, dll, MAX_PATH);
        wchar_t* sep = wcsrchr(dll, L'\\');
        if (sep) *(sep + 1) = 0;
        wcscat_s(dll, MAX_PATH, L"hookdll.dll");
    }
    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"[!] dll not found: %s\n", dll);
        return 1;
    }
    wchar_t full[MAX_PATH];
    GetFullPathNameW(dll, MAX_PATH, full, nullptr);

    wprintf(L"FreeHook by madect\n");
    wprintf(L"[*] open Discord and JOIN A VOICE CHANNEL...\n");

    DWORD pid = 0;
    for (int i = 0; i < 120 && !pid; ++i) {
        pid = FindVoicePid();
        if (!pid) { Sleep(1000); wprintf(L"."); fflush(stdout); }
    }
    if (!pid) { wprintf(L"\n[!] timeout, join a voice channel first.\n"); return 2; }
    wprintf(L"\n[+] Discord pid: %u\n", pid);

    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                              PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                              PROCESS_VM_READ, FALSE, pid);
    if (!proc) { wprintf(L"[!] OpenProcess: %lu\n", GetLastError()); return 3; }

    SIZE_T bytes = (wcslen(full) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(proc, nullptr, bytes,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(proc, remote, full, bytes, nullptr)) {
        wprintf(L"[!] write failed: %lu\n", GetLastError());
        if (remote) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc); return 4;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th) {
        wprintf(L"[!] CreateRemoteThread: %lu\n", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE); CloseHandle(proc); return 5;
    }

    if (WaitForSingleObject(th, 15000) != WAIT_OBJECT_0) {
        // поток ещё читает путь из remote — не освобождаем, пусть утечёт
        wprintf(L"[!] LoadLibrary timed out.\n");
        CloseHandle(th); CloseHandle(proc); return 6;
    }
    DWORD code = 0; GetExitCodeThread(th, &code);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(th); CloseHandle(proc);

    if (!code) { wprintf(L"[!] load failed.\n"); return 7; }
    wprintf(L"[+] injected. press Insert to show/hide the window.\n");
    return 0;
}
