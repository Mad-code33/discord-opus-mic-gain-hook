// Filterless: гоним аудио-пайплайн Discord в «raw».
//
// 1) Экспортируемые сеттеры discord_voice.node — по имени, поэтому работает на
// любой версии (stable/PTB/Canary). __fastcall-методы Discord:: : RCX = this,
// RDX = значение (bool в DL, uint32 в EDX). Зовём оригинал с тем же this, но с
// нужным значением. Discord дёргает их только при заходе в войс и смене
// настроек, а мы инжектимся уже в войсе — поэтому запоминаем последний this и
// значение и по чекбоксу переотправляем сами.
//   false: NoiseSuppression, NoiseCancellation (+DuringProcessing),
//     AutomaticGainControl, EchoCancellation, EnableBuiltInAEC,
//     EchoCancellationPreEcho, SidechainCompression, DuckingPreference (bool =
//     «разрешить ducking»: IAudioSessionControl2::SetDuckingPreference(!bool)).
//   true:  AudioInputBypassSystemProcessing (обойти обработку ОС).
//   0:     VoiceSampleRateCap (0 = без потолка; иначе кадр мика ресемплится
//     вниз до 8/16/32k — по сути low-pass).
//
// 2) AudioProcessingImpl::ApplyConfig — через неё идёт любое изменение WebRTC
// APM. Не экспорт: ищем по строке лога "AudioProcessing::ApplyConfig: ".
// Гасим enabled всех стадий в копии Config (0xB4 байт) и зовём оригинал.
//
// 3) Connection::ApplySettings — Attenuation дискорда («приглушать другие
// приложения»). Офсет поля в ConnectionSettings плывёт между версиями
// (stable 0x618, canary 0x620) — выводим из JS-парсера настроек.
//
// HPF (WebRTC HighPassFilter + opus VOIP hp_cutoff) и opus AUDIO — патчи кода,
// patches.cpp (PATCH_FILTERLESS).
//
// Stereo-патчеры на диске меняют первый байт сеттера 41 → C3 (ret), и
// MinHook трамплин над ret не строит. Хвост пролога родной — возвращаем 41 в
// памяти, хукаем как обычно, при выгрузке ставим C3 обратно.

#include "hook.h"
#include "MinHook.h"

#include <cstring>
#include <iterator>

// крутилка из gui: true — глушим фильтры
extern "C" volatile bool g_filterless = false;

namespace {

using Set_t = void (__fastcall*)(void* self, uintptr_t v);

struct Setter {
    const char*         name;     // decorated-имя экспорта
    uint32_t            forced;   // что ставим при filterless
    Set_t               orig;     // трамплин
    uint8_t*            stub;     // экспорт, если вернули C3 → 41
    void* volatile      self;     // последний this от дискорда
    volatile uintptr_t  last;     // что дискорд просил последним
};

Setter g_set[] = {
    { "?SetNoiseSuppression@Discord@@QEAAX_N@Z",                  false },
    { "?SetNoiseCancellation@Discord@@QEAAX_N@Z",                 false },
    { "?SetNoiseCancellationDuringProcessing@Discord@@QEAAX_N@Z", false },
    { "?SetAutomaticGainControl@Discord@@QEAAX_N@Z",              false },
    { "?SetEchoCancellation@Discord@@QEAAX_N@Z",                  false },
    { "?EnableBuiltInAEC@Discord@@QEAAX_N@Z",                     false },
    { "?SetEchoCancellationPreEcho@Discord@@QEAAX_N@Z",           false },
    { "?SetSidechainCompression@Discord@@QEAAX_N@Z",              false },
    { "?SetDuckingPreference@Discord@@QEAAX_N@Z",                 false },  // optOut → без ducking
    { "?SetAudioInputBypassSystemProcessing@Discord@@QEAAX_N@Z",  true  },  // bypass OS-обработки
    { "?SetVoiceSampleRateCap@Discord@@QEAAXI@Z",                 0     },  // без потолка частоты
};

int g_stuck = 0;   // C3 на диске, но хвост чужой — не вернуть, не хукнуть

template <int I> void __fastcall Detour(void* s, uintptr_t v) {
    Setter& f = g_set[I];
    f.self = s;
    f.last = v;
    f.orig(s, g_filterless ? f.forced : v);
}
void* const kDetours[] = {
    (void*)&Detour<0>, (void*)&Detour<1>, (void*)&Detour<2>, (void*)&Detour<3>, (void*)&Detour<4>,
    (void*)&Detour<5>, (void*)&Detour<6>, (void*)&Detour<7>, (void*)&Detour<8>, (void*)&Detour<9>,
    (void*)&Detour<10>,
};
static_assert(std::size(kDetours) == std::size(g_set), "detour на каждый сеттер");

// родной пролог после 41: push r15/r14/r12/rsi/rdi/rbx; sub rsp, imm32
const uint8_t kTail[] = { 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC };

// --- APM: AudioProcessingImpl::ApplyConfig(this, const Config*) ---
using ApplyCfg_t = void (__fastcall*)(void* apm, const uint8_t* cfg);
constexpr size_t kCfg = 0xB4;   // sizeof(AudioProcessing::Config): memcpy в ApplyConfig
// enabled: pre_amp, capture_level_adj, analog_mic_gain_emu, high_pass, echo,
// enforce_hpf, ns, transient, agc1, agc1 analog, agc2, input_volume, agc2 adaptive
const uint8_t kCfgOff[] = { 0x10, 0x18, 0x24, 0x2C, 0x2E, 0x30, 0x34, 0x40, 0x44, 0x58, 0x94, 0x95, 0x98 };
const uint8_t kCfgPro[] = { 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC };

ApplyCfg_t     o_cfg = nullptr;
void* volatile g_apm = nullptr;
uint8_t        g_cfg[kCfg];   // последний конфиг от дискорда, как есть
SRWLOCK        g_cfgLock = SRWLOCK_INIT;

void ApplyCfg(void* apm, const uint8_t* cfg) {
    uint8_t c[kCfg];
    memcpy(c, cfg, kCfg);
    if (g_filterless) for (uint8_t o : kCfgOff) c[o] = 0;
    o_cfg(apm, c);
}
void __fastcall h_cfg(void* apm, const uint8_t* cfg) {
    AcquireSRWLockExclusive(&g_cfgLock);
    memcpy(g_cfg, cfg, kCfg);
    g_apm = apm;
    ReleaseSRWLockExclusive(&g_cfgLock);
    ApplyCfg(apm, cfg);
}
// ponytail: один apm (последний); гонка с ApplyConfig самого дискорда в момент
// клика может вернуть прошлый конфиг — APM сам под локом, следующий вызов поправит
void ReapplyCfg() {
    uint8_t c[kCfg];
    AcquireSRWLockShared(&g_cfgLock);
    void* apm = g_apm;
    memcpy(c, g_cfg, kCfg);
    ReleaseSRWLockShared(&g_cfgLock);
    if (!apm) return;
    __try { ApplyCfg(apm, c); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}   // apm протух — не роняем дискорд
}

// --- Connection::ApplySettings: attenuation ---
const char kSet1[] = "?ApplySettings@Connection@voice@discord@@QEAAXAEBUConnectionSettings@3@@Z";
const char kSet2[] = "?ApplySettings@Connection@voice@discord@@QEAAXAEBUConnectionSettings@3@"
                     "V?$basic_string_view@DU?$char_traits@D@std@@@std@@@Z";
using Set1_t = void (__fastcall*)(void* conn, uint8_t* s);
using Set2_t = void (__fastcall*)(void* conn, uint8_t* s, void* sv);   // string_view (16 б) — по указателю

Set1_t o_set1 = nullptr;
Set2_t o_set2 = nullptr;
size_t g_attn = 0;   // ConnectionSettings: optional<Attenuation> engaged; 0 — не нашли

// attenuation { bool enabled @-0xC; float factor @-8; u32 self<<1|others @-4 }, engaged @0.
// JS-парсер: lea rdx,"attenuateWhileSpeakingOthers" → mov byte [rsp+X],1 (engaged);
// база B — lea rdi,[rsp+B]; lea r8,[rsp+..]; mov rdx,rdi; call ApplySettings(sv).
// lea r8 бывает disp32 (stable 9262) и disp8 (canary 1234)
size_t FindAttn(HMODULE m) {
    uint8_t* lea = FindLeaTo(m, "attenuateWhileSpeakingOthers");
    auto* apply = (uint8_t*)GetProcAddress(m, kSet2);
    if (!lea || !apply) return 0;
    __try {
        int32_t x = 0;
        for (uint8_t* p = lea; p < lea + 0x80 && !x; ++p) {
            if (p[0] == 0xC6 && p[1] == 0x84 && p[2] == 0x24 && p[7] == 0x01) x = *(int32_t*)(p + 3);
        }
        for (uint8_t* p = lea + 16; x && p < lea + 0x4000; ++p) {
            if (p[0] != 0x48 || p[1] != 0x89 || p[2] != 0xFA || p[3] != 0xE8 ||
                p + 8 + *(int32_t*)(p + 4) != apply) continue;
            uint8_t* q = p[-5] == 0x4C && p[-4] == 0x8D && p[-3] == 0x44 && p[-2] == 0x24 ? p - 13
                       : p[-8] == 0x4C && p[-7] == 0x8D && p[-6] == 0x84 && p[-5] == 0x24 ? p - 16 : nullptr;
            if (q && q[0] == 0x48 && q[1] == 0x8D && q[2] == 0xBC && q[3] == 0x24) {
                const int32_t off = x - *(int32_t*)(q + 4);
                return off > 0x400 && off < 0x1000 ? (size_t)off : 0;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

struct Attn { uint8_t* en; uint32_t* fl; uint8_t en0; uint32_t fl0; };

// гасим на время вызова (настройки копируются внутри), потом возвращаем:
// структура const& вызывающего
bool AttnOff(uint8_t* s, Attn& a) {
    if (!g_filterless || !g_attn) return false;
    __try {
        if (s[g_attn] != 1) return false;
        a.en = s + g_attn - 0xC;
        a.fl = (uint32_t*)(s + g_attn - 4);
        if (*a.en > 1 || *a.fl > 3) return false;   // не тот layout — не трогаем
        a.en0 = *a.en; a.fl0 = *a.fl;
        *a.en = 0; *a.fl = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall h_set1(void* c, uint8_t* s) {
    Attn a;
    const bool k = AttnOff(s, a);
    o_set1(c, s);
    if (k) { *a.en = a.en0; *a.fl = a.fl0; }
}
void __fastcall h_set2(void* c, uint8_t* s, void* sv) {
    Attn a;
    const bool k = AttnOff(s, a);
    o_set2(c, s, sv);
    if (k) { *a.en = a.en0; *a.fl = a.fl0; }
}

}  // namespace

// MinHook уже инициализирован (InstallHook). Ставим что нашли, enable — там же.
void InstallFilterHooks(HMODULE voice) {
    for (size_t i = 0; i < std::size(g_set); ++i) {
        Setter& f = g_set[i];
        auto* p = (uint8_t*)GetProcAddress(voice, f.name);
        if (!p) continue;   // имя не нашлось — пропускаем, не фейлим весь хук
        if (p[0] == 0xC3) {
            const uint8_t push = 0x41;
            if (memcmp(p + 1, kTail, sizeof kTail) != 0 || !WriteCode(p, &push, 1)) { ++g_stuck; continue; }
            f.stub = p;
        }
        MH_CreateHook(p, kDetours[i], (LPVOID*)&f.orig);
    }

    uint8_t* lea = FindLeaTo(voice, "AudioProcessing::ApplyConfig: ");
    uint8_t* cfg = lea ? FuncStart(lea) : nullptr;
    // пролог — защита от куска-продолжения / чужой функции
    if (!cfg || memcmp(cfg, kCfgPro, sizeof kCfgPro) != 0 ||
        MH_CreateHook(cfg, (void*)&h_cfg, (LPVOID*)&o_cfg) != MH_OK) o_cfg = nullptr;

    g_attn = FindAttn(voice);
    if (g_attn) {
        auto* s1 = (void*)GetProcAddress(voice, kSet1);
        auto* s2 = (void*)GetProcAddress(voice, kSet2);
        if (s1) MH_CreateHook(s1, (void*)&h_set1, (LPVOID*)&o_set1);
        if (s2) MH_CreateHook(s2, (void*)&h_set2, (LPVOID*)&o_set2);
    }
}

// после MH_DisableHook пролог снова 41 — возвращаем стаб с диска
void RestoreDiskStubs() {
    const uint8_t ret = 0xC3;
    for (Setter& f : g_set) {
        if (f.stub && f.stub[0] == 0x41) WriteCode(f.stub, &ret, 1);
    }
}

// чекбокс: переотправить всем сеттерам, которые дискорд уже звал, APM-конфиг
// и HPF/opus-патчи. Attenuation применится со следующих настроек дискорда.
void SetFilterless(bool on) {
    g_filterless = on;
    for (Setter& f : g_set) {
        void* s = f.self;
        if (!s) continue;
        __try { f.orig(s, on ? f.forced : f.last); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}   // this протух — не роняем дискорд
    }
    if (o_cfg) ReapplyCfg();
    SetPatches(PATCH_FILTERLESS, on);
}

void FilterCounts(int& stuck, int& seen, bool& apm, bool& attn) {
    stuck = g_stuck;
    seen = 0;
    for (const Setter& f : g_set) seen += f.self != nullptr;
    seen += g_apm != nullptr;
    apm = o_cfg != nullptr;
    attn = g_attn != 0;
}
