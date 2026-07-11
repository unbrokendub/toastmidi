#pragma once
#include <Arduino.h>

// ============================================================
//  TOAST — конфигурация под вашу плату
//
//  Значения ниже — ЗАГЛУШКИ-ПРИМЕР. Прошейте toast_probe,
//  пройдите меню (s, m, k, l, n, p) и замените блок между
//  маркерами «ВСТАВЬТЕ ЭТО» на то, что напечатал зонд.
// ============================================================

#define MIDI_CH 0            // стартовый MIDI-канал 0..15 (0 = «канал 1»)

struct PotDef { uint8_t src; uint8_t chan; uint8_t cc; };
struct KeyDef { uint8_t src; uint8_t chan; uint8_t activeLow; uint8_t note; };
// src:  индекс в MUX_Z[]
//       0xFF = аналоговый вход напрямую (chan = пин, например A3)
//       0xFE = цифровой пин напрямую (chan = пин; при activeLow=1
//              включается внутренняя подтяжка INPUT_PULLUP)
// cc:   номер MIDI CC;  note: номер MIDI-ноты

// ---- Распиновка платы пользователя (вывод probe от 2026-07-11) ----

#define MUX_S0 A1
#define MUX_S1 A2
#define MUX_S2 A3

static const uint8_t MUX_Z[] = {A0, A6, A8};

static const PotDef POTS[] = {
  // поле cc = стартовый CC-номер (меняется на лету: SHIFT+OCT-DOWN + пот)
  {0, 0, 1},    // POT1  верхний ряд, крайний левый (R24)
  {0, 5, 2},    // POT2  верхний ряд, у гнезда MIDI
  {1, 6, 3},    // POT3  верхний ряд, третий (R13)
  {1, 5, 4},    // POT4  верхний ряд, правый (R11)
  {0, 6, 5},    // POT5  средний ряд, левый (R25)
  {0, 7, 6},    // POT6  центр (R21)
  {1, 0, 7},    // POT7  правее центра (R12)
  {1, 7, 8},    // POT8  правый край (R10)
  {1, 3, 0},    // POT9  правый нижний (R9) — селектор VALUE, CC не шлёт
};

static const KeyDef KEYS[] = {
  // поле note в v0.1 не используется (роли кнопок — ниже)
  {1, 1, 1, 36},  // KEY1  (подтверждено повторным прогоном зонда 11.07.2026)
  {1, 2, 1, 37},  // KEY2
  {1, 4, 1, 38},  // KEY3
  {2, 2, 1, 39},  // KEY4
  {2, 1, 1, 40},  // KEY5
  {2, 3, 1, 41},  // KEY6
  {2, 7, 1, 42},  // KEY7
  {2, 5, 1, 43},  // KEY8
  {2, 4, 1, 44},  // KEY9
  {2, 0, 1, 45},  // KEY10
  {2, 6, 1, 46},  // KEY11
};

// ---- роли органов управления (v0.1 «скейл-клавиатура») ----
// индексы = номер KEY/POT минус 1

#define BTN_SHIFT     3      // KEY4  — шифт
#define BTN_OCT_DOWN 10      // KEY11 — октава вниз (левый нижний угол)
#define BTN_OCT_UP    0      // KEY1  — октава вверх (правый нижний угол)

// игровые кнопки слева направо, как удобно играть:
// KEY6, KEY10, KEY5, KEY9, KEY8, KEY7, KEY3, KEY2
static const uint8_t NOTE_KEYS[8] = {5, 9, 4, 8, 7, 6, 2, 1};

// пот, закреплённый за игровой кнопкой (для SHIFT+пот = нота кнопки):
// KEY6-POT5, KEY10-POT1, KEY5-POT6, KEY9-POT2,
// KEY8-POT7, KEY7-POT3,  KEY3-POT8, KEY2-POT4
static const uint8_t KEY_POT[8] = {4, 0, 5, 1, 6, 2, 7, 3};

#define POT_VALUE 8          // POT9 — селектор VALUE

#define PIN_LED 6            // белый светодиод

#define NRF_CSN_PIN 9        // подтверждено зондом (STATUS/CONFIG совпали)
#define NRF_CE_PIN  5        // кандидаты: 5, 7, 0 — уточняется при включении радио

// ---- конец блока распиновки ----

#define N_MUX  (sizeof(MUX_Z) / sizeof(MUX_Z[0]))
#define N_POTS (sizeof(POTS) / sizeof(POTS[0]))
#define N_KEYS (sizeof(KEYS) / sizeof(KEYS[0]))

// ---- OLED ----
#define OLED_HEIGHT 32       // 0.91" = 128x32; поставьте 64, если экран 128x64
#define OLED_ADDR 0x3C

// ---- NRF24L01 (беспроводной MIDI, фаза 2) ----
#define ENABLE_NRF 0         // 1 — когда известны CE/CSN и собран приёмник
#define NRF_CHANNEL 76
#define NRF_ADDR "TOAST"

// ---- поведение ----
#define POT_SEND_THRESHOLD 6   // защита от дребезга АЦП (в отсчётах 0..1023)
#define POT_ARM_THRESHOLD 16   // насколько сдвинуть пот, чтобы он «проснулся»
                               // после смены режима (защита от скачков)
#define KEY_DEBOUNCE_MS 15
#define KEY_VELOCITY 127
#define OVERLAY_MS 1400        // сколько держать подсказку на экране
