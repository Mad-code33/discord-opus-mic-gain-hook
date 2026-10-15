# FreeHook — Discord Opus Mic Gain Hook

Discord microphone gain / loud mic hook for Windows — boost your mic volume
before Opus encoding.

Хук микрофона для Discord: крутилка усиления (gain), и всё. Открытый исходник.
**created by madect**

<p align="center">
  <img src="HookGUI.png" alt="FreeHook — Discord mic gain hook GUI" width="380">
</p>

## Что это

FreeHook — лёгкий **Discord mic gain hook** для Windows. DLL цепляется к функции
кодирования голоса (Opus) в `discord_voice.node` и домножает твой PCM на
коэффициент усиления **до** энкодера — тебя слышат громче, обработка идёт на
твоей стороне (microphone booster / loud mic).

## Возможности (Features)

- **Mic gain / усиление микрофона** 1x – 16x с пресетами.
- Обработка **до Opus-энкода** (`WebRtcOpus_Encode_wrapper`) — чистый буст.
- **Filterless / raw audio** (чекбокс): глушим noise suppression, noise
  cancellation, AGC, echo cancellation (+ built-in AEC, pre-echo, sidechain
  compression), high-pass filter, **ducking** Windows и включаем bypass
  системной обработки. Сеттеры берём по **имени экспорта** `discord_voice.node`
  — работает на stable/PTB/Canary без правки RVA. Применяется сразу, без
  перезахода в войс (после первого вызова сеттера дискордом). Плюс:
  - хук `AudioProcessing::ApplyConfig` — все стадии WebRTC APM выключены в
    любом конфиге, который шлёт дискорд (HPF, NS, AEC, AGC1/AGC2, transient,
    pre-amp, capture level);
  - **Attenuation** дискорда («приглушать другие приложения») выключен
    (`Connection::ApplySettings`);
  - `SetVoiceSampleRateCap` → 0: мик не ресемплится вниз (без скрытого low-pass);
  - Opus в режиме **AUDIO** вместо VOIP (энкодер без голосовой обработки).
  Attenuation и Opus AUDIO — со следующего захода в войс.
- **Stereo / HQ** (чекбокс): stereo-патчи накатываются **в памяти** — стерео в
  SDP, всегда 48k. Плюс Opus на максимум: complexity 10, fullband, без DTX.
  Файл на диске не трогаем, снимается чекбоксом и при выгрузке. Подробнее —
  [ниже](#stereo--hq-в-freehook).
- Лёгкий **ImGui-оверлей** поверх окна Discord (topmost, без иконки в taskbar).
- Поиск энкодера по **RVA + сигнатуре (AOB)** — переживает смену версии Discord.
- **Чистая выгрузка** DLL по крестику — всё возвращается как было.
- Без лимитера: громкость крутишь сам.

## Скачать (Download)

Готовые `injector.exe` и `hookdll.dll` — на странице
[**Releases**](https://github.com/Mad-code33/discord-opus-mic-gain-hook/releases/latest).
Собирать ничего не нужно — скачал и запустил.

## Как работает

- `injector.exe` ищет процесс `Discord.exe` (а также PTB / Canary) с загруженным `discord_voice.node`
  (значит ты в войсе) и грузит `hookdll.dll` через `CreateRemoteThread` +
  `LoadLibraryW`.
- `hookdll.dll` ставит хук на `WebRtcOpus_Encode_wrapper` через
  [MinHook](https://github.com/TsudaKageyu/minhook), а при включённом
  **Filterless** — ещё и на сеттеры DSP (см. ниже).
- В перехвате каждый сэмпл умножается на `gain`, клампится в `int16`, дальше
  зовётся оригинал.
- Адрес ищется по RVA от базы модуля, а если версия Discord другая — сканом по
  сигнатуре пролога (первое уникальное совпадение). Окно — Dear ImGui на DX11.

## Адрес и сигнатура

```
модуль:  discord_voice.node

WebRtcOpus_Encode_wrapper
  RVA:   0x8492b0            # Discord stable 1.0.9262 (9255-9261: 0x7950a0)
  proto: int __fastcall (
             void*    opus_inst,            // RCX
             int16_t* pcm,                  // RDX  — буфер для правки
             uint64_t samples_per_channel,  // R8   (gate: cmp r8, 0x1680)
             int      max_encoded_bytes,    // R9
             uint8_t* encoded_out)          // stack
  пролог (16 байт):
    56 57 48 83 EC 28 BF FF FF FF FF 49 81 F8 80 16

  каналы:    opus_inst + 0x10 (uint64, 1..8)
  размер:    samples_per_channel * channels
```

## Filterless — экспортируемые сеттеры

Глушатся перехватом экспортов `discord_voice.node` (имена стабильны между
версиями, RVA ниже — для справки, под 1.0.9261). Сигнатура у всех одна:
`void __fastcall(void* this /*RCX*/, value /*RDX: bool в DL, uint32 в EDX*/)` —
хук зовёт оригинал с форс-значением, переиспользуя `this` самого Discord.

```
# форсим в false (стадия выключена):
?SetNoiseSuppression@Discord@@QEAAX_N@Z                   RVA 0x949d0
?SetNoiseCancellation@Discord@@QEAAX_N@Z                  RVA 0x953a0
?SetNoiseCancellationDuringProcessing@Discord@@QEAAX_N@Z  RVA 0x95f30
?SetAutomaticGainControl@Discord@@QEAAX_N@Z               RVA 0x95080
?SetEchoCancellation@Discord@@QEAAX_N@Z                   RVA 0x940a0
?EnableBuiltInAEC@Discord@@QEAAX_N@Z                      RVA 0x93da0
?SetEchoCancellationPreEcho@Discord@@QEAAX_N@Z            RVA 0x943a0
?SetSidechainCompression@Discord@@QEAAX_N@Z              RVA 0xb1800
?SetDuckingPreference@Discord@@QEAAX_N@Z                  RVA 0xa9830   # no ducking
# форсим в true (обойти обработку ОС):
?SetAudioInputBypassSystemProcessing@Discord@@QEAAX_N@Z   RVA 0xa5560
# форсим в 0 (без потолка частоты мика):
?SetVoiceSampleRateCap@Discord@@QEAAXI@Z                  RVA 0xa8830
```

Поверх сеттеров (не экспорты, см. [Ещё offsets](#ещё-offsets-ida-stable-109261--canary-11228)):
`AudioProcessingImpl::ApplyConfig` — 13 `enabled`-флагов APM в 0 (ищется по
строке лога, проверяется пролог); `Connection::ApplySettings` (экспорт) —
attenuation `enabled=0, flags=0`, офсет выводится из JS-парсера настроек; Opus
`EncoderCreate` 800h → 801h (AUDIO), два сайта в `patches.cpp`.

- **Ducking**: bool у `SetDuckingPreference` = «разрешить ducking». Воркер
  (0x19b350) кладёт его в obj+5 и зовёт `IAudioSessionControl2::SetDuckingPreference`
  (vtable +0x80) с `optOut = value ^ 1` → `false` = Windows не приглушает
  остальные звуки при разговоре. `prioritySpeakerDucking` (ConnectionSettings) —
  про то, что ты слышишь от priority speaker, не мик; не трогаем.
- **Сразу, без перезахода**: дискорд зовёт сеттеры только при входе в войс и
  смене настроек, а инжект идёт уже в войсе. Хук запоминает последний `this` и
  значение, чекбокс переотправляет их сам (форс или то, что просил дискорд).
  Пока дискорд ни одного не звал — в GUI `rejoin voice / toggle a voice setting once`.
- **Сеттеры `ret` на диске** (stereo-патчер: первый байт 41 → C3): если хвост
  пролога родной (`57 41 56 41 54 56 57 53 48 81 EC`), в памяти возвращаем 41,
  хукаем как обычно, при выгрузке ставим C3 обратно. Хвост чужой — сеттер
  пропускается, в GUI `pre-patched on disk`. На этом stable так застаблены 6 из
  10 (NS, NC, NC-during-processing, AGC, EC, pre-echo); `SetAutomaticGainControlConfig`
  не bool и не хукается — остаётся стабом (AGC всё равно гасит хук ApplyConfig).
- **High-pass** экспорта не имеет — патч кода, по сигнатуре (как Stereo / HQ,
  `hookdll/patches.cpp`), пишется при включении, снимается при выключении и выгрузке:

```
webrtc::HighPassFilter::Process  48 89 CF 48 8B 05 ?? ?? ?? ?? 48 31 E0 48 89 44 24 40 48 8B 42 48 45 84 C0
  начало = hit - 0x0C            41 → C3 (ret). Уже 48 B8 .. C3 (диск) — не трогаем
opus VOIP hp_cutoff ветка        81 7F 70 00 08 00 00 44 8B 87 94 00 00 00 48 63 D1 49 8D 4C 95 00 4C 8B 75 F8
  сайт = hit + 0x1A              0F 85 A1 01 00 00 → E9 A2 01 00 00 00 (jne → jmp, та же цель)
                                 уже E9 (диск) — не трогаем
APM ProcessStream gate (захват)  4C 89 6C 24 ?? 44 0F B6 AC 24 B0 01 00 00 48 8B 8E ?? ?? 00 00 31 C0 80 BC 24 B8 01 00 00 00
  hit - 0x0F: je                 74 0D → 90 90
  hit - 0x06: jg skip            0F 8F → 90 E9 (rel32 тот же) — APM не зовётся вовсе
```

Гейт — в функции захвата (S9261 0xe9050 / S9262 0xf3960 / C 0xf3e00):
`if (!frame_muted || muted_count <= 9) apm->ProcessStream(...)` (vtable +0x80,
перед ним +0xB8 / +0xC8 — stream delay / key pressed). Патч = всегда ветка «muted»:
сырой PCM мимо NS/AEC/AGC/HPF и **без сведения в моно**. Это и есть «mono gate»
Stereo-Fix. В 9262 компилятор продублировал проверку: первая копия гейтит Krisp
(`krisp/noise_canceller.cpp`, его гасит `SetNoiseCancellation`), сигнатура
привязана ко второй. Pre-patched stable 9261 (другие регистры) — `n/a`.

## Дополнительные offsets (качество / стерео) — не форсим

Эти — на выбор юзера (значение не bool), поэтому их хуки не ставим, только
документируем. Сигнатуры: `_N`=bool, `I`=uint32, `M`=float.

```
?SetVoiceChannelCountCap@Discord@@QEAAXI@Z       RVA 0xa8b40   # =2 → стерео
?SetVoiceSampleRateCap@Discord@@QEAAXI@Z         RVA 0xa8830   # потолок sample rate (Filterless → 0)
?SetHasFullbandPerformance@Discord@@QEAAX_N@Z    RVA 0xb3d20   # пишет глобалку, которую никто не читает
?SetMicVolume@Discord@@QEAAXM@Z                  RVA 0xa7ec0   # float
?SetAutomaticGainControlConfig@Discord@@QEAAXUGainControllerConfig@media@discord@@@Z  RVA 0x94ce0
?SetAudioInputEnabled@Discord@@QEAAX_N@Z         RVA 0xb1500   # НЕ трогать (вход вкл/выкл)
```

> `SetHasFullbandPerformance` — тривиальный сеттер глобалки
> (`mov [rip+x], dl; ret`). Остальные bool-сеттеры маршалят значение в
> аудио-поток.

## Opus encoder controls (кросс-ссылка на libopus)

`discord_voice.node` статически линкует libopus (`third_party/opus/src`).
WebRTC-обёртки `WebRtcOpus_Set*` не экспортируются, но находятся по строкам
ассертов `RTC_CHECK(0 == WebRtcOpus_SetX(...))` → ближайший `call` перед
строкой. Все проверены по CTL-константам libopus: обёртка зовёт
`opus_encoder_ctl(enc, <ctl>, value)` через диспетчер `opus_encoder_ctl`
@ **RVA 0x9b1d20**. Вызов `__fastcall`: RCX = inst, EDX/RDX = value.

RVA — под stable 1.0.9255-9261. Так как функции не экспортируются, для хука
нужен скан по сигнатуре; уникальный якорь — иммедиат CTL `mov edx, <ctl>`
(напр. `BA A2 0F 00 00` для bitrate), пролог у них общий.

```
func (WebRtcOpus_)   RVA        opus_encoder_ctl        полезно для
SetBitRate           0x795110   OPUS_SET_BITRATE   4002  форс макс битрейта (≤510000)
SetComplexity        0x7954a0   OPUS_SET_COMPLEXITY 4010 форс 10 (макс качество)
SetBandwidth         0x795580   OPUS_SET_BANDWIDTH  4008 форс 1105 = FULLBAND
SetForceChannels     0x7955d0   OPUS_SET_FORCE_CHANNELS 4022  форс 2 = СТЕРЕО (валид 0..2)
DisableCbr           0x795450   OPUS_SET_VBR 4006 → 1   VBR вкл (качество музыки)
EnableCbr            0x795400   OPUS_SET_VBR 4006 → 0   CBR
EnableDtx            0x7952d0   OPUS_SET_DTX 4016 → 1   DTX (режет тишину)
DisableDtx           0x795320   OPUS_SET_DTX 4016 → 0   DTX off = непрерывно
EnableFec            0x795230   OPUS_SET_INBAND_FEC 4012 → 1
DisableFec           0x795280   OPUS_SET_INBAND_FEC 4012 → 0
SetPacketLossRate    0x795160   OPUS_SET_PACKET_LOSS_PERC 4014
SetMaxPlaybackRate   0x7951b0   OPUS_SET_MAX_BANDWIDTH 4004  (rate→band: 1105=FB)
Encode_wrapper       0x7950a0   (сам энкод — уже хукается для gain)
```

Для профиля «максимум качества» (как в Stereo-Fix): `SetBitRate`→высокий,
`SetForceChannels`→2, `SetComplexity`→10, `SetBandwidth`→1105, VBR вкл
(`DisableCbr`), DTX off. Эти — на энкодере, непроверены вживую, поэтому в хук
пока не вшиты (gain/filterless от них не зависят).

RVA версионно-зависим: если не сошлось — работает скан по сигнатуре; если
пролог поменялся, обнови байты под свою версию `discord_voice.node`.

Canary 1.0.1228 (`discord_voice.node` 22.3MB) — те же функции:

```
func                 stable     canary
Encode_wrapper       0x7950a0   0x851d60   (пролог-сигнатура уникальна в обоих)
SetBitRate           0x795110   0x851dd0
SetPacketLossRate    0x795160   0x851e20
SetMaxPlaybackRate   0x7951b0   0x851e70
EnableFec            0x795230   0x851ef0
DisableFec           0x795280   0x851f40
EnableDtx            0x7952d0   0x851f90
DisableDtx           0x795320   0x851fe0
EnableCbr            0x795400   0x8520c0
DisableCbr           0x795450   0x852110
SetComplexity        0x7954a0   0x852160
SetBandwidth         0x795580   0x852240
SetForceChannels     0x7955d0   0x852290
```

## libopus внутри (stable / canary)

```
opus_encode               0x9b1a40 / 0xa9ea20  Encode_wrapper зовёт, если inst+0x0 != 0
opus_multistream_encode   0x9b5a60 / 0xaa2a40  ... иначе этот (inst+0x8)
opus_encode_native        0x9ad230 / 0xa9a210
opus_encode_frame_native  0x9aee40 / 0xa9be20  hp_cutoff и dc_reject заинлайнены сюда
opus_encoder_ctl          0x9b1d20 / 0xa9ed00
downmix_int               0x9aca30 / 0xa99a10  адрес — lea в opus_encode перед call encode_native
OpusEncoder: +0x70 application (0x800 VOIP / 0x801 AUDIO), +0x74 channels, +0x94 Fs
Encode_wrapper inst: +0x18 in_dtx flag
```

## Stereo-Fix patch sites (то, что патчат stereo-патчеры)

Найдено диффом stable против canary: на этой машине stable
`discord_voice.node` **уже пропатчен на диске** сторонним stereo-патчером, canary —
чистый. Колонки: оригинал (canary) → что лежит в stable. Сигнатуры уникальны в
обоих билдах и матчат и чистый, и пропатченный код.

```
что                         stable    canary    orig → patched
AudioEncoderOpusImpl::SdpToConfig (S 0x5ed180 / C 0x675290)
  каналы по умолчанию       0x5ed346  0x675456  BD 01 00 00 00 → BD 02 00 00 00   (mov ebp,1 → 2)
  проверка fmtp "stereo"    0x5ed353  0x675463  75 6B → EB 6B                     (jne → jmp)
  min(maxplaybackrate,48k)  0x5ed4ae  0x6755be  44 0F 42 F0 → 90×4                (cmovb → nop, всегда 48k)
  дефолтный битрейт         0x5ed75f  0x67586f  41 байт расчёта → BD C0 C8 03 00 90×32 44 8B F5 90
                                                (ebp = r14d = 248000)
AudioEncoderOpusImpl::SetTargetBitrate (S 0x5ef850 / C 0x677960)
  значение битрейта         0x5ef89e  0x6779ae  48 B9 00000000 01000000 → 48 B9 C0C80300 00000000
  optional has_value        0x5ef8a8  0x6779b8  48 09 C1 → 90×3   (has_value=0 → дальше битрейт не меняется)
  bitrate multiplier        0x5ef8de  0x6779ee  F3 0F 59 44 81 EC → 90×6
webrtc::HighPassFilter::Process  0x5fa7c0  0x681ad0  пролог → 48 B8 <imm64> C3   (no-op)
rtc FatalLog (RTC_CHECK)         0x30a840  0x334990  41 → C3   (проваленный CHECK не роняет)
opus downmix_int                 0x9aca30  0xa99a10  41 → C3
opus VOIP hp_cutoff ветка        0x9af269  0xa9c249  0F 85 A1 01 00 00 → E9 A2 01 00 00 00
                                                (всегда dc_reject вместо VOIP high-pass)
mono downmix gate                0xe9236   0xf4090   test/je/cmp [..],9/jg (17 б) → 90×12 E9 rel32
Discord::Set* (7 шт., см. Filterless)          первый байт 41 → C3
  SetEchoCancellation, SetEchoCancellationPreEcho, SetNoiseSuppression,
  SetAutomaticGainControl, SetAutomaticGainControlConfig, SetNoiseCancellation,
  SetNoiseCancellationDuringProcessing

сигнатура (AOB)                                                            сайт
48 89 D9 48 89 FA E8 ?? ?? ?? ?? BD ?? 00 00 00 80 BC 24 80 01 00 00 01 ?? 6B   +0x0B каналы, +0x18 stereo jne
3D 80 BB 00 00 ?? ?? ?? ?? 44 89 B4 24 C8 00 00 00                               +0x05 48k
48 8D 9C 24 38 01 00 00 4C 8D 44 24 58 48 89 D9 48 89 FA E8 ?? ?? ?? ??         +0x18 битрейт SDP
89 F8 48 B9 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 48 89 4E 44                         +0x02 imm64, +0x0C or
F3 0F 2A C7 ?? ?? ?? ?? ?? ?? F3 0F 2C D0                                       +0x04 multiplier
81 7F 70 00 08 00 00 44 8B 87 94 00 00 00 48 63 D1 49 8D 4C 95 00 4C 8B 75 F8    +0x1A hp_cutoff ветка
48 89 CF 48 8B 05 ?? ?? ?? ?? 48 31 E0 48 89 44 24 40 48 8B 42 48 45 84 C0       -0x0C HighPassFilter::Process
```

FatalLog проще искать по строке `"# Fatal error in: %s, line %d"` (lea → функция).
Mono downmix gate: сигнатура выше (9262 + canary 1228); pre-patched 9261 — только RVA.

Заметки:
- Пропатченные в `C3` сеттеры MinHook не хукает (трамплин упирается в `ret`) —
  Filterless возвращает им 41 в памяти (см. [Filterless](#filterless--экспортируемые-сеттеры)).
- `Set{NoiseSuppression,EchoCancellation,AutomaticGainControl}(int)` — thunk'и
  (`cmp edx,-1; setne dl; jmp <bool-версия>`), bool-хук их уже покрывает.
- Ещё экспорты, не задействованные в хуке: `SetNoAudioInputThreshold(float)`
  0xa8500, `SetLocalVoiceLevelChangedCallback` 0xa93b0 (уровень мика для GUI),
  `SetEchoReferenceMode` 0x946b0, `UpdateFieldTrial(name,value)` 0xb1fa0,
  `Connection::SetUserPlayoutVolume(id,float)` 0x1895e0.

## Stereo / HQ в FreeHook

Чекбокс **Stereo / HQ** пишет в память (`VirtualProtect` + байты, остальные
потоки заморожены на время записи) 3 сайта SdpToConfig из таблицы выше
(HPF и hp_cutoff — под Filterless). Таблица с сигнатурами —
`hookdll/patches.cpp`, место ищется **только по AOB** (RVA не используются).

```
SdpToConfig  каналы      BD 01 → BD 02
SdpToConfig  "stereo"    75 → EB              (jne → jmp)
SdpToConfig  48k         44 0F 42 F0 → 90×4   (cmovb → nop)
```

Плюс хуки opus-обёрток (не патч байт, `hookcore.cpp`), пока галка стоит:
`WebRtcOpus_SetComplexity` → 10, `WebRtcOpus_SetBandwidth` → 1105 (fullband),
`WebRtcOpus_EnableDtx` → вместо него `DisableDtx`. Ищутся по прологу + ctl
(`mov edx, 0FAAh / 0FA8h / 0FB0h`). Дискорд зовёт их при создании энкодера, так
что тоже со следующего захода в войс. В GUI (подсказка) — сколько из 3 нашлось.

- Сигнатуры нет или она не уникальна — сайт пропускается (`n/a` в GUI).
- Там не оригинал (уже пропатчено на диске) — не трогаем (`disk` в GUI). На
  этом stable все 3 уже на диске, чекбокс там ничего не меняет.
- Снять галку / крестик (выгрузка dll) — возвращаются оригинальные байты, только
  там, где писали мы.
- Стерео / 48k читаются при согласовании кодека: после включения **перезайди в
  войс**.
- Не берём: битрейт (`SetTargetBitrate` гасит optional `has_value`), FatalLog→ret
  (прячет настоящие падения), `downmix_int` (кормит только tonality-анализ opus).
- Mono gate (обход APM) — под **Filterless**, не Stereo: он заодно выключает
  NS/AEC. Без Filterless APM может свести стерео-вход в моно.

Проверка сигнатур офлайн (парсит таблицу из `patches.cpp`, по умолчанию — stable
1.0.9262 и Canary 1.0.1234 в `%LOCALAPPDATA%`):

```cmd
python tools\sigcheck.py [путь\к\discord_voice.node ...]
```

## Ещё offsets (IDA, stable 1.0.9261 / canary 1.0.1228)

Найдено в IDA по строкам ассертов WebRTC. ApplyConfig, attenuation и Opus AUDIO
уже под **Filterless** (`hookdll/filters.cpp`, `patches.cpp`); `tools/sigcheck.py`
проверяет и их поиск в рантайме.

Что не фильтр, а **гейт**: Input Sensitivity дискорда (`vadThreshold`,
`vadAutoThreshold`, `vadUseKrisp` в `inputModeOptions`) режет тихие куски по
порогу. Хуком не форсим — это открытый мик всегда; полностью raw = в дискорде
выключить «Automatically determine input sensitivity» и увести порог влево (или
PTT). `SetHasFullbandPerformance` — мёртвая глобалка, её никто не читает.

```
EngineAudioTransport::SetVoiceSampleRateCap  this+0xFCCC (0 = без потолка)
  читает S 0x13bd20: rate = min(rate, cap) → округление до 8/16/32/48k
EngineAudioTransport::SetVoiceChannelCountCap this+0xFCD0
```

### Opus application: VOIP → AUDIO (1 байт)

`WebRtcOpus_EncoderCreate` (S 0x794e50 / C 0x851b10) и Multistream-версия
(S 0x794f80 / C 0x851c40) выбирают `mov ebp, 800h` (VOIP) или `801h` (AUDIO)
перед `opus_encoder_create` (S 0x9ac8f0 / C 0xa998d0). Discord шлёт kVoip.

```
AOB: 45 85 C0 74 11 41 83 F8 01 0F 85 ?? ?? ?? ?? BD 01 08 00 00 EB 05 BD 00 08 00 00
     2 хита в каждом билде (обычный + multistream), сайт = hit+0x17: 00 → 01
S 0x794e96, 0x794fca   C 0x851b56, 0x851c8a
```

AUDIO-режим сам убирает VOIP `hp_cutoff` в Opus. `OPUS_SET_APPLICATION` после
первого кадра не принимается, поэтому патчить нужно до создания энкодера
(rejoin voice).

### AudioEncoderOpusImpl (WebRTC)

```
RecreateEncoderInstance  S 0x5ee520 / C 0x676630  (строка "WebRtcOpus_EncoderCreate( &inst_, ...")
SetTargetBitrate         S 0x5ef850 / C 0x677960
this+0x30 frame_size_ms  +0x34 sample_rate_hz  +0x38 num_channels (size_t)
    +0x40 application (0=kVoip)  +0x44 bitrate_bps (optional<int>, +0x48 has)
    +0x4C fec  +0x4D cbr  +0x50 max_playback_rate_hz  +0x54 complexity
    +0x58 low_rate_complexity  +0x64 dtx  +0xB0 packet_loss_rate (float)
    +0xD0 inst_ (WebRtcOpusEncInst*)  +0xEC complexity_ (текущая)
```

### AudioProcessing::ApplyConfig — 100% filterless одним хуком

`AudioProcessingImpl::ApplyConfig(this /*RCX*/, const Config* /*RDX*/)` — через неё
идёт **любое** изменение APM. Хук: скопировать 0xB4 байт, обнулить флаги, позвать
оригинал. Найти: xref на строку `"AudioProcessing::ApplyConfig: "`.

```
ApplyConfig  S 0x430970 / C 0x467b60   (пролог одинаковый:
  41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC 88 00 00 00 0F 29 74 24 70 49 89 D6 48 89 CF)
Config::ToString  S 0x5f9940 / C 0x680c50
sizeof(Config) = 0xB4 (memcpy в this+0x178); раскладка одинаковая в stable 9261 / 9262
и canary 1228 / 1234 (сверено по Config::ToString):
  +0x00 pipeline.maximum_internal_processing_rate (int)
  +0x04 multi_channel_render   +0x05 multi_channel_capture (1 = не сводить вход в моно)
  +0x10 pre_amplifier.enabled            +0x14 fixed_gain_factor (float)
  +0x18 capture_level_adjustment.enabled +0x1C pre_gain_factor (float) +0x20 post_gain_factor (float)
  +0x24 analog_mic_gain_emulation.enabled +0x28 initial_level
  +0x2C high_pass_filter.enabled
  +0x2E echo_canceller.enabled           +0x30 enforce_high_pass_filtering
  +0x34 noise_suppression.enabled        +0x38 ns level (0..3)
  +0x40 transient_suppression.enabled
  +0x44 gain_controller1.enabled         +0x48 agc1 mode   +0x4C target_level_dbfs
  +0x50 compression_gain_db              +0x54 agc1 enable_limiter
  +0x58 agc1 analog_gain_controller.enabled  (+0x5C..+0x90 — clipping predictor и т.п.)
  +0x94 gain_controller2.enabled         +0x95 input_volume_controller.enabled
  +0x98 agc2 adaptive_digital.enabled
```

Стерео **без** обхода APM (NS/AEC живы): в хуке ApplyConfig ставить `+0x05 = 1`.
Работает, пока `multi_channel_capture_support` (ниже, `this+0x801`) = 1 — по
умолчанию так, если дискорд не включил kill switch. Пока не вшито.

`AudioProcessingImpl` ctor (S9261 0x42db00) читает field trials через
инжектированный `FieldTrialsView` (vtable +8 = Lookup) и кладёт в `this`:

```
+0x800  multi_channel_render_support   = !"WebRTC-ApmExperimentalMultiChannelRenderKillSwitch"
+0x801  multi_channel_capture_support  = !"WebRTC-ApmExperimentalMultiChannelCaptureKillSwitch"
+0x802  full-band HPF kill switch       =  "WebRTC-FullBandHpfKillSwitch"
+0x803  use muted state                 = !"WebRTC-MutedStateKillSwitch"
+0x804  enforce 48 kHz processing       =  "WebRTC-ApmEnforce48kHzProcessingRate"
```

Значение trial считается включённым, если строка начинается с `Enabled`.

### ConnectionSettings → Connection::ApplySettings (attenuation, битрейт)

Экспорты по имени (`?ApplySettings@Connection@voice@discord@@...`, S 0x18c8d0 и
0x18c050), RDX = `const ConnectionSettings*`. Сюда же приходит **Attenuation**
Discord'а («приглушать другие приложения»). Поля разъезжаются между версиями:

```
поле                                 stable   canary
encodingVoiceBitRate (opt<int>)      +0x600   +0x608   (has_value +4)
selfMute (bool)                      +0x608   +0x610
attenuation.enabled (bool)           +0x60C   +0x614
attenuationFactor (float)            +0x610   +0x618
attenuate flags (self<<1 | others)   +0x614   +0x61C
attenuation optional engaged         +0x618   +0x620
callMinBitRate / callBitRate / callMaxBitRate (opt<int>)
                                     +0x4D8/+0x4E0/+0x4E8   +0x4E0/+0x4E8/+0x4F0
qos (bool)                           +0x500   +0x508
minimumOutputDelay (opt<int>)        +0x144   +0x144
```

No attenuation: если engaged — `enabled=0`, `flags=0`. Офсеты берутся из
JS-парсера настроек (S 0x24250 / C 0x24220): `lea r8,"<ключ>"` → следующая
запись `mov [rsp+X], ...` → минус база `lea rdx,[rsp+B]` перед call ApplySettings
(S B=0x398, C B=0x3A8).

Новые билды (сверено офлайн, `tools/sigcheck.py`):

```
                                     stable 9262   canary 1234
attenuation optional engaged         +0x620        +0x648
attenuation.enabled / factor / flags engaged -0xC / -0x8 / -0x4 (в обоих)
encodingVoiceBitRate (opt<int>)      +0x608        +0x630   = engaged - 0x18 (has_value -0x14)
JS-парсер база B                     0x3A8         0x3B8
```

В canary 1234 `lea r8,[rsp+..]` перед call ApplySettings закодирован disp8
(`4C 8D 44 24 xx`), а не disp32. `FindAttn` принимает оба варианта.

Куда идут битрейты: воркер настроек (task из ApplySettings → S9261 0xf4aa0,
`connection.cpp`) копирует `callMin/Bit/MaxBitRate` в `Connection+0x5AC/+0x5A8/+0x5B0`.
`encodingVoiceBitRate` напрямую по `+0x600` никто не читает: настройки сначала
копируются во вложенную структуру. Офсет есть (engaged − 0x18), а путь до
энкодера не прослежен — форсить битрейт через хук ApplySettings пока вслепую.

### WebRTC field trials / `Discord::UpdateFieldTrial`

`?UpdateFieldTrial@Discord@@QEAAXV?$basic_string@...@0@Z` (S9261 0xb1fa0):
`(Discord* this, std::string name, std::string value)`. Строки передаются по
значению (MSVC: указатель на временный объект, callee его разрушает).
`"WebRTC-Audio-Mixer-PriorityConfig"` обрабатывается отдельно, остальное уходит
таской на voice-поток. `this` — тот же `Discord*`, что ловят хуки сеттеров
Filterless. Trials читаются при создании объектов (APM ctor выше, энкодер), то
есть изменение действует со следующего захода в войс. Что в бинаре полезно
для мика:

```
WebRTC-ApmEnforce48kHzProcessingRate        APM обрабатывает на 48k (без split-band 32k)
WebRTC-ApmExperimentalMultiChannelCaptureKillSwitch  НЕ включать: гасит стерео-захват в APM
WebRTC-FullBandHpfKillSwitch                HPF только в нижней полосе
WebRTC-Audio-OpusBitrateMultipliers         "Enabled-<m5k>-<m6k>-..." множители битрейта
                                            (их и NOP'ит Stereo-Fix; по умолчанию trial выключен)
WebRTC-Audio-Red-For-Opus                   RED (избыточность) для Opus
WebRTC-Audio-OpusDecodeStereoByDefault      приём: декодер в стерео (не мик)
WebRTC-Audio-Allocation / -PriorityBitrate  min/max битрейт аудио в BWE
```

Не проверено вживую, что UpdateFieldTrial доходит до того же `FieldTrialsView`,
который получает APM.

## Аудио-карта (stable 1.0.9261 / 1.0.9262, canary 1.0.1234)

RVA функций по всему пути мик → энкодер (и немного приёма). Колонка «якорь» —
как найти функцию в следующей версии: строка (`lea` на неё → начало функции по
.pdata), сигнатура из `patches.cpp`/`hookcore.cpp` или вызов. Все RVA 9262/1234
найдены по этим якорям, а колонка 9261 сверена с IDA (совпала с известными).

### Захват (мик → APM → отправка)

```
функция                                   9261      9262      c1234     якорь
EngineAudioTransport::RecordedDataIsAvailable
  (int16 с устройства → AudioFrame)       0x13b850  0x13cb20  0x13dfd0  единственный caller CapturedAudioProcessor::Process
EngineAudioTransport::OnLoopbackFrame     0x13c4f0  0x13d7b0  0x13ec60  "EngineAudioTransport::OnLoopbackFrame residue: "
CapturedAudioProcessor::Process
  (Krisp → APM ProcessStream, гейт)       0xe9050   0xf3960   0xf4290   сигнатура APM gate (patches.cpp)
CapturedAudioProcessor: статы/NC/AEC лог  0xe9380   0xf3f90   0xf48b0   "Audio frame samples: "
krisp NoiseCanceller::Process (+ соседняя) 0xcc740   0xd2e90   0xd3600   "Noise cancellation failed"
                                          0xccb30   0xd3110   0xd3880
APM::ProcessStream(int16*, StreamConfig)  0x4314e0  0x458bc0  0x462e10  "AudioProcessing::ProcessStream_StreamConfig"
APM::ProcessStream(AudioFrame*)           0x432e50  0x45a530  0x464780  "AudioProcessing::ProcessStream_AudioFrame"
APM::ApplyConfig                          0x430970  0x458050  0x4622a0  "AudioProcessing::ApplyConfig: "
AudioProcessingImpl ctor (field trials)   0x42db00  0x4551e0  0x45f430  "WebRTC-ApmEnforce48kHzProcessingRate"
APM Config::ToString                      0x5f9940  0x66be10  0x67f580  "AudioProcessing::Config{ pipeline: ..."
webrtc::HighPassFilter::Process           0x5fa7c0  0x66cc90  0x680400  сигнатура HPF (patches.cpp)
AudioSendStream::SendAudioData            0x4540f0  0x47b710  0x485960  "AudioSendStream::SendAudioData"
ChannelSend::ProcessAndEncodeAudio        0x628180  0x69bd90  0x6af500  "ChannelSend::ProcessAndEncodeAudio"
AudioEngine::SetMicVolume (воркер)        0x10fa00  0x11d480  0x11e860  maxss/minss → movss [rcx+63Ch]
```

### Opus (WebRTC-обёртка + libopus)

```
функция                                   9261      9262      c1234     якорь
AudioEncoderOpusImpl::SdpToConfig         0x5ed180  0x6608a0  0x673bc0  сигнатура stereo (patches.cpp)
  GetBitrateMultipliers                   0x5edbf0  0x661310  0x674630  "WebRTC-Audio-OpusBitrateMultipliers"
  RecreateEncoderInstance                 0x5ee520  0x661c40  0x674f60  "0 == WebRtcOpus_EncoderCreate( &inst_, ..."
  SetTargetBitrate / ApplyBitrate         0x5ef850  0x662f70  0x676290  "0 == WebRtcOpus_SetBitRate( inst_, GetMultipliedBitrate(..."
  SetPacketLoss (2-я функция)             0x5eeb10  0x662230  0x675550  "0 == WebRtcOpus_SetPacketLossRate( inst_, ..."
  SetForceChannels (num_channels_to_encode) 0x5ef220 0x662940 0x675c60  "0 == WebRtcOpus_SetForceChannels(inst_, num_channels_to_encode)"
  bandwidth update                        0x5efe00  0x663520  0x676840  "0 == WebRtcOpus_SetBandwidth(inst_, *bandwidth)"
  SetDtx / SetFec                         0x5eef60  0x662680  0x6759a0  "0 == WebRtcOpus_EnableDtx(inst_)"
                                          0x5eeeb0  0x6625d0  0x6758f0  "0 == WebRtcOpus_EnableFec(inst_)"
  лог target bitrate / overhead           0x5efa50  0x663170  0x676490  "AudioEncoderOpusImpl: Overhead unknown, ..."
AudioEncoderMultiChannelOpusImpl::Recreate 0x5f1ff0 0x665710  0x678a30  "0 == WebRtcOpus_MultistreamEncoderCreate( ..."
WebRtcOpus_EncoderCreate                  0x794e50  0x849060  0x8540f0  сигнатура opus AUDIO (patches.cpp)
WebRtcOpus_MultistreamEncoderCreate       0x794f80  0x849190  0x854220  то же, 2-й хит
WebRtcOpus_Encode (Encode_wrapper, gain)  0x7950a0  0x8492b0  0x854340  пролог (hookcore.cpp)
WebRtcOpus_SetBitRate         [ctl 4002]  0x795110  0x849320  0x8543b0  mov edx,0FA2h рядом с Encode_wrapper
WebRtcOpus_SetPacketLossRate  [ctl 4014]  0x795160  0x849370  0x854400
WebRtcOpus_SetMaxPlaybackRate [ctl 4004]  0x7951b0  0x8493c0  0x854450
WebRtcOpus_Enable/DisableFec  [ctl 4012]  0x795230  0x849440  0x8544d0  / 0x795280 0x849490 0x854520
WebRtcOpus_Enable/DisableDtx  [ctl 4016]  0x7952d0  0x8494e0  0x854570  / 0x795320 0x849530 0x8545c0
WebRtcOpus_Enable/DisableCbr  [ctl 4006]  0x795400  0x849610  0x8546a0  / 0x795450 0x849660 0x8546f0
WebRtcOpus_SetComplexity      [ctl 4010]  0x7954a0  0x8496b0  0x854740
WebRtcOpus_SetBandwidth       [ctl 4008]  0x795580  0x849790  0x854820
WebRtcOpus_SetForceChannels   [ctl 4022]  0x7955d0  0x8497e0  0x854870
opus_encoder_create                       0x9ac8f0  0xa91e80  0xa9b450  call из WebRtcOpus_EncoderCreate
opus_encode / opus_multistream_encode     0x9b1a40  0xa96fd0  0xaa05a0  call из Encode_wrapper
                                          0x9b5a60  0xa9aff0  0xaa45c0
opus_encode_native                        0x9ad230  0xa927c0  0xa9bd90  call из opus_encode; "opus_encoder.c"
opus_encode_frame_native                  0x9aee40  0xa943d0  0xa9d9a0  сигнатура hp_cutoff (patches.cpp)
opus_encoder_ctl                          0x9b1d20  0xa972b0  0xaa0880  самый частый call из WebRtcOpus_Set*
```

### Приём / воспроизведение

```
AudioTransportImpl::NeedMorePlayData      0x436180  0x45d7f0  0x467a40  "AudioTransportImpl::NeedMorePlayData"
AudioTransportImpl resample (play/capture) 0x4363b0 0x45da20  0x467c70  "destination.data().size() == frame.num_channels_ * ..."
ChannelReceive::GetAudioFrameWithInfo     0x48f1f0  0x4b6900  0x4c0b50  "ChannelReceive::GetAudioFrameWithInfo"
```

### Структуры

```
webrtc::AudioFrame (sizeof 0x3C60, одинаково во всех трёх)
  +0x00 u32 timestamp          +0x08 i64 elapsed_time_ms   +0x10 i64 ntp_time_ms
  +0x18 size_t samples_per_channel   +0x20 int sample_rate_hz   +0x28 size_t num_channels
  +0x30 speech_type (4 = undefined)  +0x34 vad_activity (2 = unknown)
  +0x38 optional<i64> absolute_capture_timestamp_ms
  +0x48 int16 data[7680] (0x3C00 б, interleaved)        +0x3C48 bool muted
  data() при muted отдаёт общий нулевой буфер; mutable_data() при muted чистит data.

EngineAudioTransport (this у RecordedDataIsAvailable)
  +0x7BF8 CapturedAudioProcessor*  (все три)
  +0xFCD8 i64 счётчик захваченных кадров (все три)
  синки кадра (AudioSendStream'ы), mutex / list:  9261 +0xFDD8 / +0xFE30,  9262 и c1234 +0xFE40 / +0xFE98
  RecordedDataIsAvailable(this, u32 stream, const void* pcm, size_t samples, ...,
                          u32 samples_per_sec, ...) — как webrtc::AudioTransport, плюс индекс потока

CapturedAudioProcessor                       9261    9262 / c1234
  Krisp NoiseCanceller*                      +0x68   +0x220
  NC включён (bit0)                          +0x70   +0x228
  NC: не звать Krisp на этом кадре (bit0)    +0x71   +0x229  (+0x22A, в c1234 и +0x22B — ещё флаги ветки)
  AudioProcessing* (vtable +0x80 ProcessStream,
    +0xB8 set_stream_delay_ms, +0xC8 set_stream_key_pressed)
                                             +0xC8   +0x278
  счётчик подряд-muted кадров (гейт > 9)      +0xD0   +0x280

AudioEngine (то, куда Discord::Set* шлют таски)
  +0x63C float mic volume (SetMicVolume, 0..1)   +0x2F0 mutex
  +0x340 список, по которому воркер раскидывает новую громкость
Discord (this экспортов): *this = impl; impl+0xA0 — очередь voice-потока (таски Set*),
  impl+0xC8 — трейсер (если есть, таска оборачивается с именем функции)
```

### Полезные экспорты (по имени, RVA 9262 / c1234)

```
?StartRecordingRawSamples@Discord@@...       0x0b3b60 / 0x0b42d0  колбэк(vector<int16>, size_t, int) — сырой мик
?StopRecordingRawSamples@Discord@@QEAAXXZ    0x0b40e0 / 0x0b4850
?SetLocalVoiceLevelChangedCallback@Discord@@ 0x0adfd0 / 0x0ae740  колбэк(float level, u32) — уровень мика
?SetLoopback@Discord@@QEAAX_N...             0x0b0910 / 0x0b1080  слышать себя (+ колбэк уровня)
?SetMicVolume@Discord@@QEAAXM@Z              0x0acae0 / 0x0ad250
?SetSpeakerVolume@Discord@@QEAAXM@Z          0x0ace00 / 0x0ad570
?SetNoAudioInputThreshold@Discord@@QEAAXM@Z  0x0ad120 / 0x0ad890
?SetSkipNoiseCancellationIfMuted@Discord@@   0x099c20 / 0x09a390
?GetNoiseCancellationStats@Discord@@         0x099080 / 0x0997f0
?SetEchoReferenceMode@Discord@@              0x098090 / 0x098800
?SetVoiceChannelCountCap@Discord@@QEAAXI@Z   0x0ad760 / 0x0aded0
?SetVoiceSampleRateCap@Discord@@QEAAXI@Z     0x0ad450 / 0x0adbc0
?SetAudioMixerOptions@Discord@@              0x0af240 / 0x0af9b0
?SetVoiceThreadPriority@Discord@@QEAAXH@Z    0x0b8d40 / 0x0b94b0
?UpdateFieldTrial@Discord@@                  0x0b72d0 / 0x0b7a40
?WriteAudioDebugState@Discord@@QEAAXXZ       0x0b35c0 / 0x0b3d30
?GetSystemMicrophoneMode@Discord@@           0x0aa270 / 0x0aa9e0
?SetUserPlayoutVolume@Connection@voice@discord@@...(id, float)  0x193ef0 / 0x196a50
?SetUserPlayoutPan@Connection@voice@discord@@...(id, l, r)      0x193c20 / 0x196780
?IsRemoteUserSpeaking@Connection@voice@discord@@                0x195a20 / 0x198580
```

Исходники (строки путей) для дальнейших поисков: `media/engine_audio_transport.cpp`,
`media/captured_audio_processor.cpp`, `krisp/krisp_wrapper.cpp`,
`krisp/voice_activity_detector.cpp`, `media/audio_engine.cpp`,
`media/loopback_audio_stream.cpp`, `audio_samples_recorder.cpp`; ADM — на Rust
(`discord_media/components/audio/interop/cpp/audio_device/rust_adm.cpp`).

## Сборка (Build / Installation)

Visual Studio Build Tools (MSVC + Windows SDK), путь к `vcvars64.bat` —
переменная `VCVARS` в `build.bat`.

```cmd
cd C:\...\freehook
build.bat
```

Выход: `build\hookdll.dll`, `build\injector.exe`.

## Запуск (Usage)

1. Открой Discord и зайди в голосовой канал.
2. Запусти `injector.exe`.
3. Окно **FreeHook** появится поверх Discord. **Insert** — скрыть/показать,
   **X** — снять хук и выгрузить DLL.

## Контакты

Discord — **madect**
