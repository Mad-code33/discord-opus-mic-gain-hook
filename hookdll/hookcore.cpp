#include "hook.h"
#include "MinHook.h"

#include <cstring>

Encode_t g_orig_encode = nullptr;
extern "C" volatile float g_gain     = 1.0f;
static volatile LONG g_inflight = 0;   // сколько потоков сейчас внутри хука
bool g_hookActive = false;

namespace {

// WebRtcOpus_Encode_wrapper в discord_voice.node, первые 16 байт пролога.
// RVA под Discord stable 1.0.9262 (9255-9261: 0x7950a0, canary 1234: 0x854340).
// Не сошлось — срабатывает скан по сигнатуре (пролог уникален в модуле).
constexpr uintptr_t kEncodeRva = 0x8492b0;
const uint8_t kPrologue[16] = {
    0x56, 0x57, 0x48, 0x83, 0xEC, 0x28, 0xBF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x49, 0x81, 0xF8, 0x80, 0x16
};

int16_t ClampI16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

bool BytesEq(uintptr_t a, const uint8_t* p, size_t n) {
    __try { return memcmp((void*)a, p, n) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uintptr_t Resolve(HMODULE m) {
    uintptr_t a = (uintptr_t)m + kEncodeRva;
    if (BytesEq(a, kPrologue, sizeof kPrologue)) return a;
    return Scan(m, kPrologue, nullptr, sizeof kPrologue);
}

// --- Stereo / HQ: opus-энкодер на максимум ---
// WebRtcOpus_SetComplexity / SetBandwidth / EnableDtx не экспорт: пролог +
// ctl-иммедиат (README, «Аудио-карта»). Дискорд зовёт их при создании энкодера
// (rejoin voice) и при адаптации — подменяем значение, пока включён HQ.
// tools/sigcheck.py берёт сигнатуры отсюда: строки, начинающиеся с "48 83 EC 28"
const char kSigCplx[]   = "48 83 EC 28 48 85 C9 74 ?? 41 89 D0 48 8B 01 48 85 C0 74 ?? 48 89 C1 BA AA 0F 00 00";
const char kSigBw[]     = "48 83 EC 28 48 85 C9 74 ?? 41 89 D0 48 8B 01 48 85 C0 74 ?? 48 89 C1 BA A8 0F 00 00";
const char kSigDtxOn[]  = "48 83 EC 28 48 85 C9 74 ?? 48 8B 01 48 85 C0 74 ?? 48 89 C1 BA B0 0F 00 00 41 B8 01 00 00 00";
const char kSigDtxOff[] = "48 83 EC 28 48 85 C9 74 ?? 48 8B 01 48 85 C0 74 ?? 48 89 C1 BA B0 0F 00 00 45 31 C0";

using Ctl1_t = int16_t (__fastcall*)(void* inst, int32_t v);
using Ctl0_t = int16_t (__fastcall*)(void* inst);
Ctl1_t o_cplx = nullptr, o_bw = nullptr;
Ctl0_t o_dtxOn = nullptr, p_dtxOff = nullptr;
int    g_hqHooks = 0;

int16_t __fastcall h_cplx(void* i, int32_t v) { return o_cplx(i, g_hq ? 10 : v); }
int16_t __fastcall h_bw(void* i, int32_t v)   { return o_bw(i, g_hq ? 1105 : v); }   // OPUS_BANDWIDTH_FULLBAND
int16_t __fastcall h_dtx(void* i)             { return g_hq ? p_dtxOff(i) : o_dtxOn(i); }

void InstallHqHooks(HMODULE m) {
    auto hook = [m](const char* sig, void* det, void* orig) {
        void* p = (void*)ScanSig(m, sig);
        if (p && MH_CreateHook(p, det, (LPVOID*)orig) == MH_OK) ++g_hqHooks;
    };
    hook(kSigCplx, (void*)&h_cplx, &o_cplx);
    hook(kSigBw, (void*)&h_bw, &o_bw);
    p_dtxOff = (Ctl0_t)ScanSig(m, kSigDtxOff);
    if (p_dtxOff) hook(kSigDtxOn, (void*)&h_dtx, &o_dtxOn);
}

}  // namespace

extern "C" volatile bool g_hq = false;
int HqHooks() { return g_hqHooks; }

// если по rva не сошлось (другая версия дискорда) — ищем по сигнатуре.
// mask[k] == 0 — байт любой; mask == nullptr — точное совпадение
uintptr_t Scan(HMODULE m, const uint8_t* p, const uint8_t* mask, size_t n) {
    __try {
        auto* dos = (IMAGE_DOS_HEADER*)m;
        auto* nt  = (IMAGE_NT_HEADERS64*)((uint8_t*)m + dos->e_lfanew);
        uint8_t* base = (uint8_t*)m;
        size_t size = nt->OptionalHeader.SizeOfImage;
        uintptr_t hit = 0;
        for (size_t i = 0; i + n <= size; ++i) {
            size_t k = 0;
            while (k < n && (base[i + k] == p[k] || (mask && !mask[k]))) ++k;
            if (k == n) {
                if (hit) return 0;   // не уникально — не рискуем
                hit = (uintptr_t)(base + i);
            }
        }
        return hit;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// lea r64,[rip+disp32] на строку s (с нулём) — адрес инструкции. Для
// неэкспортных функций: якорь — строка лога/ключа, RVA не нужен
uint8_t* FindLeaTo(HMODULE m, const char* s) {
    __try {
        auto* nt = (IMAGE_NT_HEADERS64*)((uint8_t*)m + ((IMAGE_DOS_HEADER*)m)->e_lfanew);
        uint8_t* base = (uint8_t*)m;
        const size_t size = nt->OptionalHeader.SizeOfImage, n = strlen(s) + 1;
        uint8_t* str = nullptr;
        for (size_t i = 0; i + n <= size && !str; ++i) {
            if (base[i] == (uint8_t)s[0] && memcmp(base + i, s, n) == 0) str = base + i;
        }
        if (!str) return nullptr;
        for (size_t i = 0; i + 7 <= size; ++i) {
            uint8_t* p = base + i;
            if ((p[0] == 0x48 || p[0] == 0x4C) && p[1] == 0x8D && (p[2] & 0xC7) == 0x05 &&
                p + 7 + *(int32_t*)(p + 3) == str) return p;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

// начало функции по .pdata (кусок-продолжение даст свой старт — проверяй пролог)
uint8_t* FuncStart(uint8_t* pc) {
    DWORD64 ib = 0;
    auto* rf = RtlLookupFunctionEntry((DWORD64)pc, &ib, nullptr);
    return rf ? (uint8_t*)(ib + rf->BeginAddress) : nullptr;
}

// перед энкодом множим pcm на гейн, жёстко клампим в int16
int __fastcall Hooked_Encode(void* inst, int16_t* pcm, uint64_t spc,
                             int maxb, uint8_t* out) {
    InterlockedIncrement(&g_inflight);

    // каналы не прочитались (сменилась структура) — pcm не трогаем,
    // иначе угаданное 2 при моно-буфере пишет за его конец
    uint64_t ch = 0;
    if (inst) {
        __try {
            uint64_t c = *(uint64_t*)((uint8_t*)inst + 0x10);
            if (c >= 1 && c <= 8) ch = c;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    const float g = g_gain;
    if (pcm && ch && g != 1.0f) {
        const uint64_t n = spc * ch;
        for (uint64_t i = 0; i < n; ++i) {
            pcm[i] = ClampI16((int32_t)(pcm[i] * g));
        }
    }

    int r = 0;
    __try { r = g_orig_encode(inst, pcm, spc, maxb, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { r = -1; }

    InterlockedDecrement(&g_inflight);
    return r;
}

bool InstallHook() {
    HMODULE voice = nullptr;
    for (int i = 0; i < 120 && !voice; ++i) {
        voice = GetModuleHandleW(L"discord_voice.node");
        if (!voice) Sleep(500);
    }
    if (!voice) return false;

    uintptr_t addr = Resolve(voice);
    if (!addr) return false;

    if (MH_Initialize() != MH_OK) return false;
    if (MH_CreateHook((LPVOID)addr, (LPVOID)&Hooked_Encode,
                      (LPVOID*)&g_orig_encode) != MH_OK) {
        MH_Uninitialize();
        return false;
    }
    InstallFilterHooks(voice);   // по возможности; критичен только энкодер
    InstallHqHooks(voice);
    InitPatches(voice);          // только резолв, пишет gui по чекбоксу
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MH_Uninitialize();
        return false;
    }
    g_hookActive = true;
    return true;
}

// true — можно выгружать dll
bool UninstallHook() {
    if (!g_hookActive) return true;
    MH_DisableHook(MH_ALL_HOOKS);   // новые вызовы идут сразу в оригинал
    g_hookActive = false;

    // трамплин (g_orig_encode) и код хука должны пережить тех, кто уже внутри
    for (int i = 0; i < 400 && g_inflight > 0; ++i) Sleep(5);
    if (g_inflight > 0) return false;   // кто-то завис — лучше утечь, чем уронить дискорд
    // ponytail: sleep закрывает пару инструкций вне счётчика (вход/выход хука);
    // строгий вариант — счётчик в asm-стабе, если когда-то реально упадёт
    Sleep(50);
    MH_Uninitialize();   // освобождает трамплин
    return true;
}
