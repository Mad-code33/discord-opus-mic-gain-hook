#pragma once

#include <windows.h>
#include <cstdint>

using Encode_t = int (__fastcall*)(void*, int16_t*, uint64_t, int, uint8_t*);

extern Encode_t g_orig_encode;

// крутилка мика (1.0 = без изменений), пишет gui, читает аудио-поток
extern "C" volatile float g_gain;
// filterless: глушить NS/AGC/EC у Discord (пишет gui, читают фильтр-хуки)
extern "C" volatile bool g_filterless;
extern bool g_hookActive;
// Stereo / HQ: opus complexity 10, fullband, без DTX (пишет gui, читают хуки обёрток)
extern "C" volatile bool g_hq;
int HqHooks();   // сколько opus-обёрток нашли и захукали (из 3)

bool InstallHook();
bool UninstallHook();   // false — кто-то ещё внутри хука, dll выгружать нельзя
uintptr_t Scan(HMODULE m, const uint8_t* p, const uint8_t* mask, size_t n);
uint8_t* FindLeaTo(HMODULE m, const char* s);
uint8_t* FuncStart(uint8_t* pc);

// filters.cpp
void InstallFilterHooks(HMODULE voice);
void RestoreDiskStubs();   // C3 обратно, после снятия хуков
void SetFilterless(bool on);
// stuck — сеттеры `ret` на диске, которые не вернуть; seen — дискорд уже звал;
// apm/attn — нашли ApplyConfig / офсет attenuation
void FilterCounts(int& stuck, int& seen, bool& apm, bool& attn);

// patches.cpp: патчи кода в памяти
enum { PATCH_STEREO, PATCH_FILTERLESS };
bool WriteCode(uint8_t* at, const uint8_t* b, size_t n);
uintptr_t ScanSig(HMODULE m, const char* sig);   // AOB-строка ("48 ?? 0F"), уникальный хит или 0
void InitPatches(HMODULE voice);
void SetPatches(int group, bool on);
void PatchCounts(int group, int& on, int& disk, int& na);

void RunGui();
