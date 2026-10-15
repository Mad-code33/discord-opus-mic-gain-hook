// Патчи кода discord_voice.node в памяти — то, что stereo-патчеры пишут на
// диск (README, «Stereo-Fix patch sites»). Файл не трогаем. Места — только по
// сигнатуре (RVA между версиями едут); снимаем чекбоксом и при выгрузке dll.
//   PATCH_STEREO     — SDP: стерео, 48k            (чекбокс Stereo / HQ)
//   PATCH_FILTERLESS — HighPassFilter, VOIP hp_cutoff, opus AUDIO, обход APM (чекбокс Filterless)
//
// Сайт пишем, только если там ровно оригинал. Не совпало — значит уже
// пропатчено на диске (сигнатура место подтвердила) или код другой: не лезем.
//
// ponytail: битрейт (SetTargetBitrate/SdpToConfig) и FatalLog→ret не берём:
// первый гасит optional has_value, второй прячет настоящие падения.
// downmix_int → ret тоже нет: он кормит только tonality-анализ opus, без него
// анализ видит тишину.

#include "hook.h"

#include <tlhelp32.h>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

enum { NA, CLEAN, DISK, ON };   // нет сигнатуры / оригинал / на диске / наш патч

struct Site {
    int         group;
    const char* sig;     // AOB как в README, ?? — любой байт
    int         off;     // от начала сигнатуры
    const char* orig;
    const char* patch;   // той же длины
    uint8_t*    at;
    int         state;
};

// формат строк держи как есть: tools/sigcheck.py парсит эту таблицу
Site g_sites[] = {
    // SdpToConfig: каналы по умолчанию (mov ebp,1 → 2) и fmtp "stereo" (jne → jmp)
    { PATCH_STEREO, "48 89 D9 48 89 FA E8 ?? ?? ?? ?? BD ?? 00 00 00 80 BC 24 80 01 00 00 01 ?? 6B", 0x0C, "01", "02" },
    { PATCH_STEREO, "48 89 D9 48 89 FA E8 ?? ?? ?? ?? BD ?? 00 00 00 80 BC 24 80 01 00 00 01 ?? 6B", 0x18, "75", "EB" },
    // SdpToConfig: min(maxplaybackrate, 48k) — cmovb → nop, всегда 48k
    { PATCH_STEREO, "3D 80 BB 00 00 ?? ?? ?? ?? 44 89 B4 24 C8 00 00 00", 0x05, "44 0F 42 F0", "90 90 90 90" },
    // opus_encode_frame_native: VOIP hp_cutoff → всегда dc_reject (jne → jmp, та же цель).
    // ponytail: rel32 вшит (одинаков в stable/canary); сдвинется — сайт покажется «disk»
    { PATCH_FILTERLESS, "81 7F 70 00 08 00 00 44 8B 87 94 00 00 00 48 63 D1 49 8D 4C 95 00 4C 8B 75 F8", 0x1A, "0F 85 A1 01 00 00", "E9 A2 01 00 00 00" },
    // webrtc::HighPassFilter::Process (void) → ret
    { PATCH_FILTERLESS, "48 89 CF 48 8B 05 ?? ?? ?? ?? 48 31 E0 48 89 44 24 40 48 8B 42 48 45 84 C0", -0x0C, "41 56 56 57", "C3 56 56 57" },
    // WebRtcOpus_EncoderCreate / MultistreamEncoderCreate: mov ebp,800h (VOIP) → 801h (AUDIO).
    // Действует на энкодеры, созданные после патча (rejoin voice)
    { PATCH_FILTERLESS, "45 85 C0 74 11 41 83 F8 01 0F 85 ?? ?? ?? ?? BD 01 08 00 00 EB 05 BD 00 08 00 00 B9 01 00 00 00 BA 20 00 00 00", 0x17, "00", "01" },
    { PATCH_FILTERLESS, "45 85 C0 74 11 41 83 F8 01 0F 85 ?? ?? ?? ?? BD 01 08 00 00 EB 05 BD 00 08 00 00 4C 8B A4 24 B8 00 00 00", 0x17, "00", "01" },
    // захват мика: test al,al; je; cmp [apm+..],9; jg skip → ProcessStream APM (vtable +0x80)
    // не зовётся вовсе — ни NS/AEC/AGC, ни сведения в моно ("mono gate" Stereo-Fix).
    // je → nop nop, jg → nop jmp (rel32 тот же). Якорь — код сразу после гейта
    { PATCH_FILTERLESS, "4C 89 6C 24 ?? 44 0F B6 AC 24 B0 01 00 00 48 8B 8E ?? ?? 00 00 31 C0 80 BC 24 B8 01 00 00 00", -0x0F, "74 0D", "90 90" },
    { PATCH_FILTERLESS, "4C 89 6C 24 ?? 44 0F B6 AC 24 B0 01 00 00 48 8B 8E ?? ?? 00 00 31 C0 80 BC 24 B8 01 00 00 00", -0x06, "0F 8F", "90 E9" },
};

// "48 ?? 0F" → байты + маска (0 — любой); ?? strtoul не парсит
size_t Hex(const char* s, uint8_t* b, uint8_t* m) {
    size_t n = 0;
    while (*s) {
        if (*s == ' ') { ++s; continue; }
        char* e;
        b[n] = (uint8_t)strtoul(s, &e, 16);
        m[n++] = e != s;
        s = e != s ? e : s + 2;
    }
    return n;
}

}  // namespace

uintptr_t ScanSig(HMODULE m, const char* sig) {
    uint8_t b[64], k[64];
    return Scan(m, b, k, Hex(sig, b, k));
}

// пишем при замороженных остальных потоках (как MinHook): никто не исполнит
// полузаписанную инструкцию и не стоит посреди переписываемых байт (nop×4)
bool WriteCode(uint8_t* at, const uint8_t* b, size_t n) {
    std::vector<DWORD> ids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te = { sizeof te };
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID == GetCurrentProcessId() &&
            te.th32ThreadID != GetCurrentThreadId()) ids.push_back(te.th32ThreadID);
    }
    CloseHandle(snap);

    for (int tries = 0; tries < 50; ++tries) {
        // память — до заморозки: замороженный поток может держать лок кучи
        std::vector<HANDLE> hs;
        hs.reserve(ids.size());
        bool busy = false;
        for (DWORD id : ids) {
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, id);
            if (!h) continue;
            if (SuspendThread(h) == (DWORD)-1) { CloseHandle(h); continue; }
            hs.push_back(h);
            CONTEXT c = {};
            c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(h, &c) && c.Rip > (DWORD64)at && c.Rip < (DWORD64)(at + n)) busy = true;
        }
        DWORD old;
        bool ok = !busy && VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old);
        if (ok) {
            memcpy(at, b, n);
            VirtualProtect(at, n, old, &old);
            FlushInstructionCache(GetCurrentProcess(), at, n);
        }
        for (HANDLE h : hs) { ResumeThread(h); CloseHandle(h); }
        if (!busy) return ok;
        Sleep(1);
    }
    return false;
}

// ponytail: сигнатура SdpToConfig сканится дважды (два сайта) — ~20 мс на старте
void InitPatches(HMODULE voice) {
    for (Site& s : g_sites) {
        uint8_t b[64], m[64], o[16];
        size_t n = Hex(s.sig, b, m);
        uintptr_t hit = Scan(voice, b, m, n);
        if (!hit) continue;   // нет или не уникальна → NA
        s.at = (uint8_t*)(hit + s.off);
        size_t k = Hex(s.orig, o, m);
        s.state = memcmp(s.at, o, k) == 0 ? CLEAN : DISK;
    }
}

void SetPatches(int group, bool on) {
    for (Site& s : g_sites) {
        if (s.group != group) continue;
        uint8_t from[16], to[16], m[16];
        size_t n = Hex(on ? s.orig : s.patch, from, m);
        Hex(on ? s.patch : s.orig, to, m);
        if (s.state != (on ? CLEAN : ON) || memcmp(s.at, from, n) != 0) continue;
        if (WriteCode(s.at, to, n)) s.state = on ? ON : CLEAN;
    }
}

void PatchCounts(int group, int& on, int& disk, int& na) {
    on = disk = na = 0;
    for (const Site& s : g_sites) {
        if (s.group != group) continue;
        on += s.state == ON; disk += s.state == DISK; na += s.state == NA;
    }
}
