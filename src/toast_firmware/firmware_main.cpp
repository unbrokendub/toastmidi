/*
 * TOAST — кастомная прошивка v0.2 «скейл-клавиатура»
 * SpaceMelodyLab TOAST v1.1 (Arduino Pro Micro / ATmega32U4)
 *
 * Управление:
 *   8 игровых кнопок       — ноты (полифония, аккорды работают)
 *   KEY11 / KEY1           — октава вниз / вверх
 *   KEY4 (SHIFT) + POT9    — выбор скейла
 *   KEY4 + POT1..8         — нота закреплённой кнопки (внутри скейла)
 *   KEY4 + KEY11 + POT9    — MIDI-канал
 *   KEY4 + KEY11 + POT1..8 — CC-номер пота
 *   KEY4 + KEY1  + POT9    — яркость OLED
 *   KEY4 + KEY1  + POT1    — тоника (root) скейла
 *   POT1..8 без шифта      — MIDI CC (по умолчанию CC1..CC8)
 *
 * Все события идут в USB-MIDI и в DIN/TRS MIDI OUT одновременно.
 * OLED: клавиатура одной октавы + статус + подсказки действий.
 * Штатная радиопанель принимается через nRF24L01+ (это не BLE).
 * Настройки сохраняются в EEPROM отложенно, без записи первых 10 байт.
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <MIDIUSB.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

#if ENABLE_NRF_RX
#include <SPI.h>
bool nrfOk = false;
#endif

Adafruit_SSD1306 display(128, OLED_HEIGHT, &Wire, -1);
bool hasOled = false;

// ================== скейлы ==================

struct ScaleDef {
  uint8_t len;
  uint8_t period;                   // 12 = октава; 19 = тритава Болен-Пирса
  uint8_t deg[13];
  char name[11];
};

const ScaleDef SCALES[] PROGMEM = {
  {12, 12, {0,1,2,3,4,5,6,7,8,9,10,11}, "CHROMATIC"},
  {7,  12, {0,2,4,5,7,9,11},            "MAJOR"},
  {7,  12, {0,2,3,5,7,8,10},            "MINOR"},
  {7,  12, {0,2,3,5,7,8,11},            "HARM MINOR"},
  {7,  12, {0,2,3,5,7,9,11},            "MEL MINOR"},
  {7,  12, {0,2,3,5,7,9,10},            "DORIAN"},
  {7,  12, {0,1,3,5,7,8,10},            "PHRYGIAN"},
  {7,  12, {0,2,4,6,7,9,11},            "LYDIAN"},
  {7,  12, {0,2,4,5,7,9,10},            "MIXOLYDIAN"},
  {7,  12, {0,1,3,5,6,8,10},            "LOCRIAN"},
  {5,  12, {0,2,4,7,9},                 "PENTA MAJ"},
  {5,  12, {0,3,5,7,10},                "PENTA MIN"},
  {6,  12, {0,3,5,6,7,10},              "BLUES MIN"},
  {6,  12, {0,2,3,4,7,9},               "BLUES MAJ"},
  {6,  12, {0,2,4,6,8,10},              "WHOLE TONE"},
  {8,  12, {0,2,3,5,6,8,9,11},          "DIM W-H"},
  {8,  12, {0,1,3,4,6,7,9,10},          "DIM H-W"},
  {7,  12, {0,1,4,5,7,8,10},            "PHRYG DOM"},
  {7,  12, {0,2,3,6,7,8,11},            "HUNGAR MIN"},
  {7,  12, {0,1,4,5,7,8,11},            "DBL HARMON"},
  {5,  12, {0,2,3,7,8},                 "HIRAJOSHI"},
  {5,  12, {0,1,5,7,10},                "IN SEN"},
  {5,  12, {0,1,5,6,10},                "IWATO"},
  {5,  12, {0,2,5,7,9},                 "YO"},
  {7,  12, {0,2,4,5,7,8,11},            "HARM MAJOR"},
  {7,  12, {0,2,4,6,7,9,10},            "LYDIAN DOM"},
  {7,  12, {0,1,3,4,6,8,10},            "ALTERED"},
  {7,  12, {0,2,3,5,6,8,10},            "LOCRIAN #2"},
  {7,  12, {0,2,4,5,7,8,10},            "MIXOLYD b6"},
  {7,  12, {0,1,3,5,7,9,10},            "DORIAN b2"},
  {8,  12, {0,2,4,5,7,9,10,11},         "BEBOP DOM"},
  {8,  12, {0,2,4,5,7,8,9,11},          "BEBOP MAJ"},
  {7,  12, {0,1,3,5,7,8,11},            "NEAPOL MIN"},
  {7,  12, {0,1,3,5,7,9,11},            "NEAPOL MAJ"},
  {7,  12, {0,1,4,6,8,10,11},           "ENIGMATIC"},
  {6,  12, {0,2,4,6,9,10},              "PROMETHEUS"},
  {6,  12, {0,3,4,7,8,11},              "AUGMENTED"},
  {6,  12, {0,1,4,6,7,10},              "TRITONE"},
  {7,  12, {0,1,4,5,6,8,11},            "PERSIAN"},
  {7,  12, {0,3,4,6,7,9,10},            "HUNGAR MAJ"},
  {7,  12, {0,2,3,6,7,9,10},            "ROMANIAN"},
  {5,  12, {0,2,5,7,10},                "EGYPTIAN"},
  {5,  12, {0,2,3,7,9},                 "KUMOI"},
  {5,  12, {0,1,3,7,8},                 "PELOG"},
  {9,  12, {0,2,3,4,6,7,8,10,11},       "MESSIAEN 3"},
  {8,  12, {0,1,2,5,6,7,8,11},          "MESSIAEN 4"},
  {6,  12, {0,1,5,6,7,11},              "MESSIAEN 5"},
  {8,  12, {0,2,4,5,6,8,10,11},         "MESSIAEN 6"},
  {10, 12, {0,1,2,3,5,6,7,8,9,11},      "MESSIAEN 7"},
  {9,  19, {0,3,4,7,9,12,13,16,18},     "BP LAMBDA"},
  {13, 19, {0,1,3,4,6,7,9,10,12,13,15,16,18}, "BP CHROMA"},
  {8,  12, {0,2,4,6,7,8,10,11},         "HARMONICS"},
  {9,  12, {0,1,3,4,5,7,8,9,11},        "TCHEREPNIN"},
  {6,  12, {0,3,5,6,7,11},              "BLUES MM7"},
  {7,  12, {0,3,5,6,7,9,10},            "BLUES HEPT"},
  {6,  12, {0,1,3,4,7,9},               "BLUES DHEX"},
  {7,  12, {0,2,3,5,6,7,10},            "BLUES MOD"},
  {8,  12, {0,2,3,5,6,7,9,10},          "BLUES OCT"},
  {7,  12, {0,1,3,5,6,7,10},            "BLUES PHRY"},
  {7,  12, {0,1,2,5,7,8,9},             "CHROM DOR"},
  {7,  12, {0,3,4,5,8,10,11},           "CHROM PHRY"},
  {7,  12, {0,1,3,4,6,8,9},             "ULTRA LOCR"},
  {8,  12, {0,2,3,5,6,7,8,11},          "ALGERIAN"},
  {7,  12, {0,1,4,5,6,9,10},            "ORIENTAL"},
  {8,  12, {0,1,4,5,6,8,10,11},         "ENIGMA 8"},
  {7,  12, {0,1,3,6,8,10,11},           "ENIGMA MIN"},
  {7,  12, {0,1,4,5,7,9,10},            "BHAIRAV"},
  {6,  12, {0,1,3,5,8,10},              "RITSU"},
  {7,  12, {0,1,4,6,7,8,11},            "PURAVI bVI"},
  {7,  12, {0,2,5,6,8,9,11},            "NOHKAN"},
  {8,  12, {0,1,3,4,5,7,8,10},          "FLAMENCO"},
  {8,  12, {0,2,3,5,6,8,10,11},         "SCHWARZ 42"},
  {7,  12, {0,1,4,5,7,9,11},            "BHAIRUBAHR"},
  {8,  12, {0,1,2,3,5,7,9,10},          "ADONAI MLK"},
  {8,  12, {0,2,3,5,7,8,9,11},          "ZIRAFKEND"},
  {8,  12, {0,2,3,5,7,8,10,11},         "UTIL MINOR"},
};
#define N_SCALES (sizeof(SCALES) / sizeof(SCALES[0]))

const char *const NOTE_NAMES[12] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// ================== состояние ==================

enum Mode : uint8_t { M_PLAY, M_SHIFT, M_SETUP, M_ROOT };

ScaleDef curScale;              // рабочая копия текущего скейла (из PROGMEM)
uint8_t  scaleIdx  = 0;
uint8_t  rootPC    = 0;         // тоника 0..11 (C..B)
int8_t   octOffset = 0;         // глобальный сдвиг октав (-3..+3)
uint8_t  midiCh    = MIDI_CH;   // 0..15
int16_t  ladderMin = 0;         // первый индекс лестницы, дающий MIDI note >= 0
int16_t  ladderMax = 127;       // последний индекс лестницы, дающий note <= 127
int16_t  keyLadder[N_NOTE_KEYS]; // позиция каждой игровой кнопки в лестнице
uint8_t  potCCnum[N_POTS];       // CC-номер каждого пота
int8_t   potToKey[N_POTS];       // обратная карта: пот -> игровая кнопка

Mode     mode = M_PLAY;
bool     swapLayers = true;     // true: поты без шифта крутят ноты, CC — под шифтом
bool     comboLatched = false;  // защёлка комбо SHIFT+обе октавы
int8_t   previewNote = -1;      // нота, выбираемая потом — подсветка на пиано
uint8_t  velocity = KEY_VELOCITY;  // velocity нот, крутится value-потом в игре
uint8_t  oledBrightness = OLED_BRIGHTNESS_DEFAULT;  // контраст SSD1306 0..255

uint16_t potFilt[N_POTS];
uint16_t potLatch[N_POTS];
bool     potArmed[N_POTS];
uint8_t  potSent7[N_POTS];
uint16_t potSentRaw[N_POTS];    // реальный deadband, а не только 7-bit квантование

bool     keyState[N_KEYS];      // подтверждённые состояния всех 11 кнопок
bool     keyRead[N_KEYS];
uint32_t keyT[N_KEYS];

// Каждый источник (кнопка корпуса или радио) может держать до CHORD_VOICES
// нот: одиночную ноту в обычной игре или целый аккорд под модификатором
// октавы. Канал запоминается вместе с нотами, поэтому смена MIDI-канала при
// удержании не оставляет зависших нот на старом канале.
struct HeldNotes {
  uint8_t note[CHORD_VOICES];    // NO_NOTE = пустой голос
  uint8_t channel;
};
constexpr uint8_t NO_NOTE = 0xFF;
HeldNotes sounding[N_NOTE_KEYS];

// Октавные кнопки срабатывают на ОТПУСКАНИЕ (тап = сдвиг октавы), а удержание
// служит модификатором аккордов. Флаг помечает, что удержание уже «потрачено»
// на аккорд или режим, и тогда на отпускании октава не сдвигается.
// [0] = октава вниз (Set A), [1] = октава вверх (Set B).
bool octConsumed[2] = {false, false};

#if ENABLE_NRF_RX
// Радиокнопка является самостоятельным источником: она может удерживаться
// одновременно с кнопкой корпуса. Отдельное состояние также подавляет
// повторы одинакового PRESS/RELEASE, которые пульт может посылать для
// надёжности доставки.
HeldNotes radioNotes[NRF_BUTTON_COUNT];
bool     radioDown[NRF_BUTTON_COUNT];
uint8_t  radioCcChannel[NRF_BUTTON_COUNT];
uint32_t radioPressedAt[NRF_BUTTON_COUNT];
uint8_t  keypadId[NRF_PANEL_COUNT][6];
bool     keypadIdValid[NRF_PANEL_COUNT];
#endif

bool     usbDirty  = false;
bool     dispDirty = true;
uint32_t ledOffAt = 0, dispLastDraw = 0, overlayUntil = 0;
char     overlay[22] = "";
char     lastEvent[22] = "TOAST v0.2";

// ================== отложенное EEPROM ==================

// На little-endian AVR эти четыре байта лежат как ASCII "TMD1".
constexpr uint32_t SETTINGS_MAGIC = 0x31444D54UL;
constexpr uint8_t SETTINGS_VERSION = 2;  // v2: добавлено поле brightness
constexpr uint8_t SETTINGS_FLAG_SWAP_LAYERS = 0x01;

struct __attribute__((packed)) StoredSettings {
  uint32_t magic;
  uint8_t version;
  uint8_t size;
  uint8_t scale;
  uint8_t root;
  int8_t octave;
  uint8_t channel;
  uint8_t noteVelocity;
  uint8_t flags;
  uint8_t brightness;
  uint8_t potCc[N_POTS];
  int16_t keyStep[N_NOTE_KEYS];
  uint16_t crc;
};

StoredSettings pendingSettings;
bool settingsDirty = false;
bool settingsWriteActive = false;
uint8_t settingsWritePos = 0;
uint32_t settingsChangedAt = 0;

static_assert(N_KEYS <= 16, "key change mask is 16 bit");
static_assert(N_NOTE_KEYS == 8, "UI and radio mapping expect eight note keys");
static_assert(POT_VALUE < N_POTS, "POT_VALUE must index POTS");
static_assert(sizeof(StoredSettings) <= 255, "EEPROM writer uses an 8-bit index");
static_assert(SETTINGS_EEPROM_START >= 10, "first ten EEPROM bytes are reserved");
static_assert(SETTINGS_EEPROM_START + sizeof(StoredSettings) <=
                  NRF_EEPROM_KEYPAD_1,
              "custom settings must not overlap official keypad IDs");

// ================== утилиты ==================

// Лёгкая сборка коротких строк без snprintf. Один вызов snprintf тянет
// avr-libc vfprintf (~900 байт flash); эти три хелпера убирают эту
// зависимость целиком. Вызывающий обязан завершить строку '\0'.
char *putStr(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
char *putU(char *p, uint16_t v) {
  char t[5];
  uint8_t n = 0;
  do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (n) *p++ = t[--n];
  return p;
}
char *putI(char *p, int16_t v) {
  if (v < 0) { *p++ = '-'; v = (int16_t)-v; }
  return putU(p, (uint16_t)v);
}

void fmtNote(char *out, uint8_t n) {          // "C#3" (C3 = MIDI 60)
  char *p = putStr(out, NOTE_NAMES[n % 12]);
  p = putI(p, (int16_t)(n / 12) - 2);
  *p = 0;
}

// Деление вниз, а не к нулю. Обычный C/C++ operator / для отрицательных
// чисел округляет к нулю, из-за чего ступени ниже root вычислялись бы
// неверно. Отрицательные k нужны, чтобы скейл действительно покрывал
// допустимые ноты у самого низа MIDI-диапазона.
int16_t floorDiv(int16_t value, uint8_t divisor) {
  int16_t quotient = value / divisor;
  if (value % divisor < 0) quotient--;
  return quotient;
}

// k-я ступень бесконечной лестницы скейла. Период повторения хранится в
// описании скейла: 12 полутонов для обычной октавы и 19 для BP-тритавы.
int16_t ladderNote(int16_t k) {
  const int16_t cycle = floorDiv(k, curScale.len);
  const uint8_t degree = (uint8_t)(k - cycle * curScale.len);
  return rootPC + curScale.period * cycle + curScale.deg[degree];
}

uint8_t playedNote(uint8_t j) {               // с учётом сдвига октав
  int16_t n = ladderNote(keyLadder[j]) + 12 * octOffset;
  while (n > 127) n -= 12;
  while (n < 0) n += 12;
  return (uint8_t)n;
}

void resetLadder() {                          // кнопки = первые 8 ступеней от ~C3
  int16_t base = curScale.len * (60 / curScale.period);
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) keyLadder[j] = base + j;
}

void setScale(uint8_t s) {
  scaleIdx = s;
  memcpy_P(&curScale, &SCALES[s], sizeof(ScaleDef));

  // Ищем полный валидный диапазон индексов, включая отрицательные.
  // Благодаря этому, например, скейл с root D может назначить C#/ниже D,
  // если эта высота действительно входит в выбранный набор ступеней.
  ladderMin = 0;
  while (ladderNote(ladderMin) >= 0) ladderMin--;
  ladderMin++;
  ladderMax = 0;
  while (ladderNote(ladderMax + 1) <= 127) ladderMax++;

  // Назначения кнопок СОХРАНЯЕМ при смене скейла/тоники: держим ту же
  // ступень-индекс каждой кнопки (в новом скейле она озвучится по-новому),
  // только подрезаем в допустимый диапазон. Полный сброс к дефолту делает
  // отдельно resetLadder() — при старте defaults и по комбо NOTES RESET.
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    if (keyLadder[j] < ladderMin) keyLadder[j] = ladderMin;
    else if (keyLadder[j] > ladderMax) keyLadder[j] = ladderMax;
  }
}

void setOverlay(const char *s) {
  strncpy(overlay, s, sizeof(overlay) - 1);
  overlay[sizeof(overlay) - 1] = 0;
  overlayUntil = millis() + OVERLAY_MS;
  previewNote = -1;             // каждая новая подсказка сама решает, что подсвечивать
  dispDirty = true;
}

bool heldEmpty(const HeldNotes &h) {
  for (uint8_t v = 0; v < CHORD_VOICES; v++) {
    if (h.note[v] != NO_NOTE) return false;
  }
  return true;
}

bool anyNotesHeld() {
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    if (!heldEmpty(sounding[j])) return true;
  }
#if ENABLE_NRF_RX
  for (uint8_t i = 0; i < NRF_BUTTON_COUNT; i++) {
    if (!heldEmpty(radioNotes[i])) return true;
  }
#endif
  return false;
}

// CRC делает оборванную запись безопасной: если питание исчезло в середине
// отложенного сохранения, на следующем старте блок не будет принят частично.
uint16_t settingsCrc(const StoredSettings &value) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
  const size_t length = sizeof(StoredSettings) - sizeof(value.crc);
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}

void setDefaultSettings() {
  rootPC = 0;
  octOffset = 0;
  midiCh = MIDI_CH;
  velocity = KEY_VELOCITY;
  swapLayers = true;              // по умолчанию поты крутят НОТЫ, CC — под шифтом
  oledBrightness = OLED_BRIGHTNESS_DEFAULT;
  setScale(0);
  resetLadder();                  // дефолтная раскладка кнопок (setScale её не трогает)
  for (uint8_t i = 0; i < N_POTS; i++) potCCnum[i] = POTS[i].cc;
}

bool storedHeaderValid(const StoredSettings &saved) {
  if (saved.magic != SETTINGS_MAGIC ||
      saved.version != SETTINGS_VERSION ||
      saved.size != sizeof(StoredSettings) ||
      saved.crc != settingsCrc(saved)) {
    return false;
  }
  if (saved.scale >= N_SCALES || saved.root >= 12 ||
      saved.octave < -3 || saved.octave > 3 || saved.channel >= 16 ||
      saved.noteVelocity < 1 || saved.noteVelocity > 127 ||
      (saved.flags & ~SETTINGS_FLAG_SWAP_LAYERS)) {
    return false;
  }
  for (uint8_t i = 0; i < N_POTS; i++) {
    if (saved.potCc[i] > 127) return false;
  }
  return true;
}

void loadSettings() {
  // Сначала формируем полностью рабочие defaults. Любая проблема с magic,
  // версией, CRC или диапазонами оставляет контроллер именно в них.
  setDefaultSettings();

  StoredSettings saved;
  EEPROM.get(SETTINGS_EEPROM_START, saved);
  if (!storedHeaderValid(saved)) return;

  rootPC = saved.root;
  setScale(saved.scale);  // одновременно пересчитывает ladderMin/ladderMax
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    if (saved.keyStep[j] < ladderMin || saved.keyStep[j] > ladderMax) {
      setDefaultSettings();
      return;
    }
  }

  octOffset = saved.octave;
  midiCh = saved.channel;
  velocity = saved.noteVelocity;
  // swapLayers сознательно НЕ восстанавливаем из EEPROM: прибор всегда
  // стартует в нот-слое (см. setDefaultSettings), а SHIFT+обе октавы даёт
  // временное переключение в CC на сессию.
  oledBrightness = saved.brightness;
  if (oledBrightness < OLED_BRIGHTNESS_MIN) oledBrightness = OLED_BRIGHTNESS_MIN;
  for (uint8_t i = 0; i < N_POTS; i++) potCCnum[i] = saved.potCc[i];
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) keyLadder[j] = saved.keyStep[j];
}

void snapshotSettings() {
  memset(&pendingSettings, 0, sizeof(pendingSettings));
  pendingSettings.magic = SETTINGS_MAGIC;
  pendingSettings.version = SETTINGS_VERSION;
  pendingSettings.size = sizeof(StoredSettings);
  pendingSettings.scale = scaleIdx;
  pendingSettings.root = rootPC;
  pendingSettings.octave = octOffset;
  pendingSettings.channel = midiCh;
  pendingSettings.noteVelocity = velocity;
  pendingSettings.flags = swapLayers ? SETTINGS_FLAG_SWAP_LAYERS : 0;
  pendingSettings.brightness = oledBrightness;
  for (uint8_t i = 0; i < N_POTS; i++) pendingSettings.potCc[i] = potCCnum[i];
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    pendingSettings.keyStep[j] = keyLadder[j];
  }
  pendingSettings.crc = settingsCrc(pendingSettings);
}

void markSettingsDirty() {
  settingsDirty = true;
  settingsChangedAt = millis();
}

bool pendingSettingsAlreadyStored() {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&pendingSettings);
  for (uint8_t i = 0; i < sizeof(StoredSettings); i++) {
    if (EEPROM.read(SETTINGS_EEPROM_START + i) != bytes[i]) return false;
  }
  return true;
}

void settingsTask() {
  // EEPROM.update() блокирует AVR примерно на несколько миллисекунд, поэтому
  // пишем максимум один байт за loop и полностью ставим процесс на паузу,
  // пока удерживается хотя бы одна проводная или беспроводная нота.
  if (anyNotesHeld()) return;

  if (settingsWriteActive) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&pendingSettings);
    EEPROM.update(SETTINGS_EEPROM_START + settingsWritePos,
                  bytes[settingsWritePos]);
    settingsWritePos++;
    if (settingsWritePos >= sizeof(StoredSettings)) settingsWriteActive = false;
    return;
  }

  if (!settingsDirty ||
      millis() - settingsChangedAt < SETTINGS_SAVE_DELAY_MS) {
    return;
  }

  // Снимок отделён от живых переменных. Если пользователь поменяет ещё одну
  // настройку уже во время записи, markSettingsDirty() оставит dirty=true,
  // и после новой тихой паузы будет записан следующий целостный снимок.
  snapshotSettings();
  settingsDirty = false;
  if (pendingSettingsAlreadyStored()) return;
  settingsWritePos = 0;
  settingsWriteActive = true;
}

// ================== чтение входов ==================

uint16_t readNode(uint8_t src, uint8_t chan) {
  uint8_t pin;
  if (src == 0xFF || src == 0xFE) {
    pin = chan;
  } else {
#if USE_FAST_PORTF_MUX && defined(__AVR_ATmega32U4__)
    static_assert(MUX_S0 == A1 && MUX_S1 == A2 && MUX_S2 == A3,
                  "fast PORTF mapping requires S0=A1, S1=A2, S2=A3");

    // На этой плате S0=A1=PF6, S1=A2=PF5, S2=A3=PF4. Поэтому простой
    // (chan << 4) развернул бы S0 и S2 и изменил уже подтверждённую probe
    // нумерацию каналов. Явно раскладываем каждый бит chan на его реальную
    // ногу, сохраняем PF7 и PF0..PF3 и меняем PF6..PF4 одной операцией.
    const uint8_t selectBits = (uint8_t)(((chan & 0x01) << 6) |
                                         ((chan & 0x02) << 4) |
                                         ((chan & 0x04) << 2));
    PORTF = (PORTF & 0x8F) | selectBits;
#else
    // Переносимый fallback для другой Arduino/другой разводки.
    digitalWrite(MUX_S0, chan & 1);
    digitalWrite(MUX_S1, (chan >> 1) & 1);
    digitalWrite(MUX_S2, (chan >> 2) & 1);
#endif

    // После переключения 74HC4051 даём аналоговому узлу установиться.
    // Первый analogRead ниже намеренно выбрасывается: он заряжает sample &
    // hold ADC от нового источника, второй уже используется как измерение.
    delayMicroseconds(50);
    pin = MUX_Z[src];
  }
  analogRead(pin);
  return (uint16_t)analogRead(pin);
}

// ================== отправка MIDI ==================

void ledFlash() {
  if (PIN_LED >= 0) {
    digitalWrite(PIN_LED, HIGH);
    ledOffAt = millis() + 25;
  }
}

void dinWrite3(uint8_t a, uint8_t b, uint8_t c) {
  Serial1.write(a);
  Serial1.write(b);
  Serial1.write(c);
}

void sendCCOnChannel(uint8_t channel, uint8_t cc, uint8_t val) {
  const uint8_t status = (uint8_t)(0xB0 | channel);

  // DIN идёт первым намеренно. USB host иногда долго не забирает endpoint;
  // MidiUSB.sendMIDI() в таком случае может ждать, но физический MIDI OUT
  // уже получит событие и не будет зависеть от поведения компьютера.
  dinWrite3(status, cc, val);
  midiEventPacket_t p = {0x0B, status, cc, val};
  MidiUSB.sendMIDI(p);
  usbDirty = true;
  ledFlash();
  char *q = putStr(lastEvent, "CC");
  q = putU(q, cc);
  q = putStr(q, " = ");
  q = putU(q, val);
  *q = 0;
  dispDirty = true;
}

void sendCC(uint8_t cc, uint8_t val) {
  sendCCOnChannel(midiCh, cc, val);
}

void sendNoteEvent(uint8_t channel, uint8_t note, bool on) {
  const uint8_t status = (uint8_t)((on ? 0x90 : 0x80) | channel);
  const uint8_t vel = on ? velocity : 0;

  dinWrite3(status, note, vel);
  midiEventPacket_t p = {(uint8_t)(on ? 0x09 : 0x08), status, note, vel};
  MidiUSB.sendMIDI(p);
  usbDirty = true;
  ledFlash();
  if (on) {
    fmtNote(lastEvent, note);
  }
  dispDirty = true;
}

bool sourceHoldsNote(const HeldNotes &h, uint8_t note, uint8_t channel) {
  if (h.channel != channel) return false;
  for (uint8_t v = 0; v < CHORD_VOICES; v++) {
    if (h.note[v] == note) return true;
  }
  return false;
}

bool anotherSourceHolds(uint8_t note, uint8_t channel,
                        const HeldNotes *ignore) {
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    if (&sounding[j] != ignore && sourceHoldsNote(sounding[j], note, channel)) {
      return true;
    }
  }
#if ENABLE_NRF_RX
  for (uint8_t i = 0; i < NRF_BUTTON_COUNT; i++) {
    if (&radioNotes[i] != ignore &&
        sourceHoldsNote(radioNotes[i], note, channel)) {
      return true;
    }
  }
#endif
  return false;
}

// Нажать до CHORD_VOICES нот как один источник. count==1 — обычная нота.
void pressNoteSet(HeldNotes &held, const uint8_t *notes, uint8_t count) {
  if (!heldEmpty(held)) return;                 // источник уже держит что-то
  held.channel = midiCh;
  for (uint8_t v = 0; v < CHORD_VOICES; v++) {
    held.note[v] = (v < count) ? notes[v] : NO_NOTE;
  }
  // Note On шлём только для нот, которых не держит другой источник, чтобы
  // повторной высоты не было. ignore=&held: собственные ноты не считаем.
  for (uint8_t v = 0; v < count; v++) {
    if (!anotherSourceHolds(held.note[v], held.channel, &held)) {
      sendNoteEvent(held.channel, held.note[v], true);
    }
  }
}

void pressNoteSource(HeldNotes &held, uint8_t note) {
  pressNoteSet(held, &note, 1);
}

void releaseNoteSource(HeldNotes &held) {
  const uint8_t channel = held.channel;
  for (uint8_t v = 0; v < CHORD_VOICES; v++) {
    const uint8_t note = held.note[v];
    if (note == NO_NOTE) continue;
    held.note[v] = NO_NOTE;                      // очищаем ДО проверки dedup
    if (!anotherSourceHolds(note, channel, nullptr)) {
      sendNoteEvent(channel, note, false);
    }
  }
}

// ================== штатная радиопанель ==================

#if ENABLE_NRF_RX
namespace NrfRx {
constexpr uint8_t R_REGISTER = 0x00;
constexpr uint8_t W_REGISTER = 0x20;
constexpr uint8_t REGISTER_MASK = 0x1F;
constexpr uint8_t R_RX_PAYLOAD = 0x61;
constexpr uint8_t FLUSH_TX = 0xE1;
constexpr uint8_t FLUSH_RX = 0xE2;
constexpr uint8_t NOP = 0xFF;

constexpr uint8_t CONFIG = 0x00;
constexpr uint8_t EN_AA = 0x01;
constexpr uint8_t EN_RXADDR = 0x02;
constexpr uint8_t SETUP_AW = 0x03;
constexpr uint8_t SETUP_RETR = 0x04;
constexpr uint8_t RF_CH = 0x05;
constexpr uint8_t RF_SETUP = 0x06;
constexpr uint8_t STATUS = 0x07;
constexpr uint8_t RX_ADDR_P1 = 0x0B;
constexpr uint8_t RX_PW_P0 = 0x11;
constexpr uint8_t FIFO_STATUS = 0x17;
constexpr uint8_t DYNPD = 0x1C;
constexpr uint8_t FEATURE = 0x1D;

SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

void select(bool active) {
  digitalWrite(NRF_CSN_PIN, active ? LOW : HIGH);
}

uint8_t command(uint8_t cmd) {
  SPI.beginTransaction(spiSettings);
  select(true);
  const uint8_t status = SPI.transfer(cmd);
  select(false);
  SPI.endTransaction();
  return status;
}

uint8_t readRegister(uint8_t reg) {
  SPI.beginTransaction(spiSettings);
  select(true);
  SPI.transfer(R_REGISTER | (reg & REGISTER_MASK));
  const uint8_t value = SPI.transfer(NOP);
  select(false);
  SPI.endTransaction();
  return value;
}

void writeRegister(uint8_t reg, uint8_t value) {
  SPI.beginTransaction(spiSettings);
  select(true);
  SPI.transfer(W_REGISTER | (reg & REGISTER_MASK));
  SPI.transfer(value);
  select(false);
  SPI.endTransaction();
}

void writeBuffer(uint8_t reg, const uint8_t *src, uint8_t length) {
  SPI.beginTransaction(spiSettings);
  select(true);
  SPI.transfer(W_REGISTER | (reg & REGISTER_MASK));
  while (length--) SPI.transfer(*src++);
  select(false);
  SPI.endTransaction();
}

void readBuffer(uint8_t reg, uint8_t *dst, uint8_t length) {
  SPI.beginTransaction(spiSettings);
  select(true);
  SPI.transfer(R_REGISTER | (reg & REGISTER_MASK));
  while (length--) *dst++ = SPI.transfer(NOP);
  select(false);
  SPI.endTransaction();
}

uint8_t readPayload(uint8_t *dst) {
  SPI.beginTransaction(spiSettings);
  select(true);
  const uint8_t status = SPI.transfer(R_RX_PAYLOAD);
  for (uint8_t i = 0; i < NRF_PAYLOAD_SIZE; i++) dst[i] = SPI.transfer(NOP);
  select(false);
  SPI.endTransaction();
  return status;
}

bool spiSelfTest() {
  const uint8_t saved = readRegister(RF_CH);
  writeRegister(RF_CH, 0x2A);
  const uint8_t first = readRegister(RF_CH);
  writeRegister(RF_CH, 0x55);
  const uint8_t second = readRegister(RF_CH);
  writeRegister(RF_CH, saved);
  return first == 0x2A && second == 0x55;
}

bool configure() {
  static_assert(NRF_ADDRESS_WIDTH == 5, "original radio uses 5-byte address");
  digitalWrite(NRF_CE_PIN, LOW);

  // Ниже намеренно повторены итоговые регистры оригинальной v1.7. Это
  // устраняет неоднозначность enum разных версий RF24 и одновременно
  // экономит flash по сравнению с включением всей TX/RX библиотеки.
  writeRegister(CONFIG, 0x0C);                 // power-down, CRC16 selected
  delay(5);
  writeRegister(EN_AA, 0x3F);                  // auto-ack all pipes
  writeRegister(EN_RXADDR, 0x02);              // only RX pipe 1
  writeRegister(SETUP_AW, 0x03);               // five-byte address
  writeRegister(SETUP_RETR, 0x5F);             // 1500 us, 15 retries
  writeRegister(RF_CH, NRF_CHANNEL);            // 120 = 2520 MHz
  writeRegister(RF_SETUP, 0x27);                // 250K, PA MAX, LNA bit
  writeRegister(DYNPD, 0x00);
  writeRegister(FEATURE, 0x00);
  for (uint8_t pipe = 0; pipe < 6; pipe++) {
    writeRegister((uint8_t)(RX_PW_P0 + pipe), NRF_PAYLOAD_SIZE);
  }
  writeBuffer(RX_ADDR_P1, NRF_RX_ADDRESS, NRF_ADDRESS_WIDTH);

  command(FLUSH_RX);
  command(FLUSH_TX);
  writeRegister(STATUS, 0x70);                  // clear all pending IRQ flags

  writeRegister(CONFIG, 0x0E);                 // PWR_UP, still PTX
  delay(5);                                     // >1.5 ms startup requirement
  writeRegister(CONFIG, 0x0F);                 // PRIM_RX
  delayMicroseconds(150);
  digitalWrite(NRF_CE_PIN, HIGH);
  delayMicroseconds(150);

  // Не ограничиваемся общим SPI self-test: читаем обратно именно те поля,
  // ошибка которых дала бы внешне «живой», но навсегда молчащий receiver.
  uint8_t address[NRF_ADDRESS_WIDTH];
  readBuffer(RX_ADDR_P1, address, sizeof(address));
  bool addressOk = true;
  for (uint8_t i = 0; i < NRF_ADDRESS_WIDTH; i++) {
    if (address[i] != NRF_RX_ADDRESS[i]) addressOk = false;
  }
  return addressOk &&
         (readRegister(CONFIG) & 0x0F) == 0x0F &&
         (readRegister(EN_AA) & 0x3F) == 0x3F &&
         (readRegister(EN_RXADDR) & 0x3F) == 0x02 &&
         (readRegister(SETUP_AW) & 0x03) == 0x03 &&
         readRegister(SETUP_RETR) == 0x5F &&
         (readRegister(RF_CH) & 0x7F) == NRF_CHANNEL &&
         (readRegister(RF_SETUP) & 0x2E) == 0x26 &&
         (readRegister(RX_PW_P0 + 1) & 0x3F) == NRF_PAYLOAD_SIZE &&
         readRegister(DYNPD) == 0x00 && readRegister(FEATURE) == 0x00;
}
}  // namespace NrfRx

bool bytesEqual(const uint8_t *a, const uint8_t *b, uint8_t length) {
  while (length--) {
    if (*a++ != *b++) return false;
  }
  return true;
}

bool idIsEmpty(const uint8_t *id) {
  for (uint8_t i = 0; i < 6; i++) {
    if (id[i] != 0xFF) return false;
  }
  return true;
}

void readOfficialKeypadIds() {
  const uint16_t addresses[NRF_PANEL_COUNT] = {
    NRF_EEPROM_KEYPAD_1, NRF_EEPROM_KEYPAD_2
  };
  for (uint8_t panel = 0; panel < NRF_PANEL_COUNT; panel++) {
    for (uint8_t i = 0; i < 6; i++) {
      keypadId[panel][i] = EEPROM.read((int)(addresses[panel] + i));
    }
    keypadIdValid[panel] = !idIsEmpty(keypadId[panel]);
  }
}

int8_t panelForPayload(const uint8_t *payload) {
  for (uint8_t panel = 0; panel < NRF_PANEL_COUNT; panel++) {
    if (keypadIdValid[panel] && bytesEqual(payload, keypadId[panel], 6)) {
      return (int8_t)panel;
    }
  }
#if NRF_ACCEPT_FALLBACK_ID
  // Живая панель этого экземпляра уже подтвердила ID "IlJRf4". Fallback
  // применяется ТОЛЬКО если официальный slot 1 пуст. После штатного re-pair
  // новый EEPROM ID становится единственным принятым, а старый пульт не
  // сможет делить radioDown/state с новой панелью.
  if (!keypadIdValid[0] &&
      bytesEqual(payload, NRF_FALLBACK_KEYPAD_ID, 6)) {
    return 0;
  }
#endif
  return -1;
}

void handleRadioButton(uint8_t logicalButton, bool down) {
  if (logicalButton >= NRF_BUTTON_COUNT ||
      radioDown[logicalButton] == down) {
    return;
  }
  radioDown[logicalButton] = down;
  radioPressedAt[logicalButton] = down ? millis() : 0;

  RadioButtonDef action;
  memcpy_P(&action, &NRF_BUTTONS[logicalButton], sizeof(action));
  switch (action.action) {
    case RADIO_ACTION_SCALE_NOTE:
      if (action.number >= N_NOTE_KEYS) return;
      if (down) {
        pressNoteSource(radioNotes[logicalButton], playedNote(action.number));
      } else {
        releaseNoteSource(radioNotes[logicalButton]);
      }
      break;

    case RADIO_ACTION_CC_MOMENTARY:
      if (action.number > 127) return;
      if (down) {
        // Канал press запоминается, чтобы release гарантированно ушёл туда
        // же даже после смены глобального MIDI channel на панели TOAST.
        radioCcChannel[logicalButton] = midiCh;
        sendCCOnChannel(radioCcChannel[logicalButton], action.number,
                        action.pressValue & 0x7F);
      } else {
        sendCCOnChannel(radioCcChannel[logicalButton], action.number,
                        action.releaseValue & 0x7F);
      }
      break;

    case RADIO_ACTION_DISABLED:
    default:
      break;
  }
}

void processRadioPayload(const uint8_t *payload) {
  const int8_t panel = panelForPayload(payload);
  if (panel < 0) return;                       // чужая/непривязанная панель

  const uint8_t event = payload[6];
  if (event & 0x80) {
    // bit7 — pairing announcement оригинальной системы. Самодельная
    // прошивка намеренно не переписывает официальные ID 0x0208..0x0213:
    // привязку делают штатной прошивкой, после чего мы её только читаем.
    return;
  }

  const uint8_t code = event & 0x3F;
  if (code >= NRF_BUTTONS_PER_PANEL) return;   // пока доказаны codes 0 и 1
  const uint8_t logicalButton =
      (uint8_t)(panel * NRF_BUTTONS_PER_PANEL + code);
  handleRadioButton(logicalButton, event & 0x40);
}

void setupRadioReceiver() {
  readOfficialKeypadIds();
  for (uint8_t i = 0; i < NRF_BUTTON_COUNT; i++) {
    for (uint8_t v = 0; v < CHORD_VOICES; v++) radioNotes[i].note[v] = NO_NOTE;
    radioNotes[i].channel = 0;
    radioDown[i] = false;
    radioCcChannel[i] = midiCh;
    radioPressedAt[i] = 0;
  }

  SPI.begin();
  nrfOk = NrfRx::spiSelfTest() && NrfRx::configure();
  if (!nrfOk) {
    digitalWrite(NRF_CE_PIN, LOW);
    *putStr(lastEvent, "NRF NOT FOUND") = 0;
    return;
  }
  *putStr(lastEvent, "RADIO READY") = 0;
}

void radioTask() {
  if (!nrfOk) return;

  // RX FIFO физически вмещает три payload. Ограничение budget защищает loop
  // от вечного чтения, если уже после старта модуль отвалится и MISO залипнет
  // в нуле (тогда ложный FIFO_STATUS мог бы выглядеть как «не пусто»).
  uint8_t budget = 3;
  bool received = false;
  while (budget-- && !(NrfRx::readRegister(NrfRx::FIFO_STATUS) & 0x01)) {
    uint8_t payload[NRF_PAYLOAD_SIZE];
    const uint8_t status = NrfRx::readPayload(payload);
    const uint8_t pipe = (status >> 1) & 0x07;
    if (pipe == 1) processRadioPayload(payload);
    received = true;
  }
  if (received || (NrfRx::readRegister(NrfRx::STATUS) & 0x40)) {
    NrfRx::writeRegister(NrfRx::STATUS, 0x40); // clear RX_DR after FIFO drain
  }

#if NRF_STUCK_HOLD_TIMEOUT_MS > 0
  // nRF auto-ack надёжен для доставленного packet, но выключенная батарея
  // между PRESS и RELEASE физически не может передать отпускание. Ограничиваем
  // такой hold, чтобы MIDI note/CC и EEPROM writer не зависли навсегда.
  const uint32_t now = millis();
  for (uint8_t i = 0; i < NRF_BUTTON_COUNT; i++) {
    if (radioDown[i] &&
        now - radioPressedAt[i] >= NRF_STUCK_HOLD_TIMEOUT_MS) {
      handleRadioButton(i, false);
    }
  }
#endif
}
#endif

// ================== кнопки ==================

int8_t noteKeyIndexOf(uint8_t keyIdx) {
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++)
    if (NOTE_KEYS[j] == keyIdx) return (int8_t)j;
  return -1;
}

// Собрать аккорд для игровой кнопки j по таблице смещений в ступенях скейла.
// Все голоса берутся из текущего скейла, поэтому любой аккорд в тональности.
// Смещение 127 (0x7F) в слоте пропускает голос. Возвращает число нот.
uint8_t buildChord(uint8_t j, const int8_t *offsets, uint8_t *out) {
  const int16_t baseK = keyLadder[j];
  uint8_t n = 0;
  for (uint8_t v = 0; v < CHORD_VOICES; v++) {
    if (offsets[v] == 127) continue;
    int16_t note = ladderNote(baseK + offsets[v]) + 12 * octOffset;
    if (note < 0 || note > 127) continue;    // крайний голос отбрасываем, не «складываем»
    out[n++] = (uint8_t)note;
  }
  return n;
}

void latchNoteAssignmentPots();
void applyBrightness();

void onKeyChange(uint8_t idx, bool down) {
  if (idx == BTN_SHIFT) { dispDirty = true; return; }

  if (idx == BTN_OCT_DOWN || idx == BTN_OCT_UP) {
    const uint8_t k = (idx == BTN_OCT_UP) ? 1 : 0;
    // Сброс флага «потрачено» на нажатии делает dispatchKeyChanges (до нот).
    // Здесь — только отпускание: короткий тап без шифта и без аккордов сдвигает октаву.
    if (!down && !octConsumed[k] && !keyState[BTN_SHIFT]) {
      const int8_t delta = (idx == BTN_OCT_UP) ? 1 : -1;
      const int8_t nextOctave = (int8_t)(octOffset + delta);
      if (nextOctave >= -3 && nextOctave <= 3) {
        octOffset = nextOctave;
        markSettingsDirty();
      }
      char b[16];
      char *p = putStr(b, "OCTAVE ");
      if (octOffset >= 0) *p++ = '+';
      p = putI(p, octOffset);
      *p = 0;
      setOverlay(b);
    }
    return;
  }

  int8_t j = noteKeyIndexOf(idx);
  if (j < 0) return;

  if (down) {
    if (mode == M_PLAY) {
      // Удержание октавной кнопки без шифта превращает нот-кнопку в аккорд.
      const bool chordDown = !keyState[BTN_SHIFT] && keyState[BTN_OCT_DOWN];
      const bool chordUp = !keyState[BTN_SHIFT] && keyState[BTN_OCT_UP];
      if (chordDown || chordUp) {
        octConsumed[chordDown ? 0 : 1] = true;   // октаву «потратили» на аккорд
        uint8_t chord[CHORD_VOICES];
        const uint8_t n =
            buildChord((uint8_t)j, chordDown ? CHORD_SET_A : CHORD_SET_B, chord);
        pressNoteSet(sounding[j], chord, n);
      } else {
        pressNoteSource(sounding[j], playedNote((uint8_t)j));
      }
    } else if (mode == M_SHIFT && j == 0) {      // SHIFT + первая кнопка
      resetLadder();
      // Уже активированные note-поты не должны на следующем loop молча
      // вернуть старые назначения. После reset их надо снова сдвинуть.
      if (!swapLayers) latchNoteAssignmentPots();
      markSettingsDirty();
      setOverlay("NOTES RESET");
    }
  } else {
    releaseNoteSource(sounding[j]);              // note-off отдаём всегда
  }
}

uint16_t scanKeys() {
  uint16_t changed = 0;
  for (uint8_t i = 0; i < N_KEYS; i++) {
    bool pressed;
    if (KEYS[i].src == 0xFE) {
      bool lvl = digitalRead(KEYS[i].chan);
      pressed = KEYS[i].activeLow ? !lvl : lvl;
    } else {
      uint16_t v = readNode(KEYS[i].src, KEYS[i].chan);
      pressed = KEYS[i].activeLow ? (v < 340) : (v > 680);
    }
    if (pressed != keyRead[i]) {
      keyRead[i] = pressed;
      keyT[i] = millis();
    }
    if (keyRead[i] != keyState[i] && millis() - keyT[i] >= KEY_DEBOUNCE_MS) {
      keyState[i] = keyRead[i];
      changed |= (uint16_t)1 << i;
    }
  }
  return changed;
}

void dispatchKeyChanges(uint16_t changed) {
  // Свежее нажатие октавной кнопки обнуляет флаг «потрачено» — но ДО раздачи
  // нот в этом же скане, иначе аккорд (индекс нот-кнопки может быть меньше)
  // затёрся бы поздним обработчиком октавы. Шифт при нажатии = сразу режим.
  if ((changed & ((uint16_t)1 << BTN_OCT_DOWN)) && keyState[BTN_OCT_DOWN]) {
    octConsumed[0] = keyState[BTN_SHIFT];
  }
  if ((changed & ((uint16_t)1 << BTN_OCT_UP)) && keyState[BTN_OCT_UP]) {
    octConsumed[1] = keyState[BTN_SHIFT];
  }

  // Сначала все нажатия, затем отпускания. Если в одном скане одна кнопка
  // передала ноту другой кнопке с тем же pitch, это не создаст короткую
  // лишнюю пару Note Off/Note On посередине.
  for (uint8_t pass = 0; pass < 2; pass++) {
    const bool wantedState = pass == 0;
    for (uint8_t i = 0; i < N_KEYS; i++) {
      if ((changed & ((uint16_t)1 << i)) && keyState[i] == wantedState) {
        onKeyChange(i, keyState[i]);
      }
    }
  }
}

// ================== режимы и поты ==================

void latchPots() {
  for (uint8_t i = 0; i < N_POTS; i++) {
    potLatch[i] = potFilt[i];
    potArmed[i] = false;
  }
}

void latchNoteAssignmentPots() {
  // Перевооружаем только восемь потов назначения нот. VALUE и возможные
  // будущие непарные контроллеры не должны побочно менять своё поведение.
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    const uint8_t i = KEY_POT[j];
    potLatch[i] = potFilt[i];
    potArmed[i] = false;
  }
}

void updateMode() {
  // SHIFT + обе октавные кнопки = поменять слои потов (CC <-> ноты)
  bool combo = keyState[BTN_SHIFT] && keyState[BTN_OCT_DOWN] &&
               keyState[BTN_OCT_UP];
  if (combo && !comboLatched) {
    comboLatched = true;
    swapLayers = !swapLayers;
    latchPots();
    // слой не сохраняем — переключение действует только до перезагрузки
    setOverlay(swapLayers ? "POTS = NOTES" : "POTS = CC");
  } else if (!combo) {
    comboLatched = false;
  }

  Mode m = M_PLAY;
  if (keyState[BTN_SHIFT]) {
    m = M_SHIFT;
    if (keyState[BTN_OCT_DOWN]) { m = M_SETUP; octConsumed[0] = true; }
    else if (keyState[BTN_OCT_UP]) { m = M_ROOT; octConsumed[1] = true; }
  }
  if (m != mode) {
    mode = m;
    latchPots();
    dispDirty = true;
  }
}

void potSendCC(uint8_t i, uint16_t f) {
  int16_t delta = (int16_t)f - (int16_t)potSentRaw[i];
  if (delta < 0) delta = -delta;
  if (delta < POT_SEND_THRESHOLD) return;

  const uint8_t v7 = (uint8_t)(f >> 3);
  if (v7 == potSent7[i]) return;
  potSentRaw[i] = f;
  potSent7[i] = v7;
  sendCC(potCCnum[i], v7);
}

void assignKeyNote(uint8_t j, uint16_t filt) {
  const uint16_t ladderSize = (uint16_t)(ladderMax - ladderMin + 1);
  const uint16_t ladderOffset =
      (uint16_t)(((uint32_t)filt * ladderSize) >> 10);
  int16_t k = (int16_t)(ladderMin + (int16_t)ladderOffset);
  if (k > ladderMax) k = ladderMax;
  if (k == keyLadder[j]) return;
  keyLadder[j] = k;
  markSettingsDirty();
  char b[20], nm[5];
  uint8_t pn = playedNote(j);
  fmtNote(nm, pn);
  char *p = putStr(b, "BTN");
  p = putU(p, (uint16_t)(j + 1));
  p = putStr(p, " = ");
  p = putStr(p, nm);
  *p = 0;
  setOverlay(b);
  previewNote = (int8_t)pn;     // показать выбираемую ноту и на клавиатуре
}

void updatePots() {
  for (uint8_t i = 0; i < N_POTS; i++) {
    uint16_t raw = readNode(POTS[i].src, POTS[i].chan);
    potFilt[i] = (potFilt[i] * 3 + raw) >> 2;
    uint16_t f = potFilt[i];

    if (!potArmed[i]) {
      int16_t d = (int16_t)f - (int16_t)potLatch[i];
      if (d < 0) d = -d;
      if (d < POT_ARM_THRESHOLD) continue;
      potArmed[i] = true;
    }

    char b[20];
    switch (mode) {
      case M_PLAY:
        if (i == POT_VALUE) {                    // value-пот в игре = velocity
          uint8_t v = (uint8_t)(f >> 3);
          if (v < 1) v = 1;                      // 0 = note-off, нельзя
          if (v != velocity) {
            velocity = v;
            markSettingsDirty();
            *putU(putStr(b, "VELOCITY "), velocity) = 0;
            setOverlay(b);
          }
          break;
        }
        if (swapLayers) {
          if (potToKey[i] >= 0) assignKeyNote((uint8_t)potToKey[i], f);
        } else {
          potSendCC(i, f);
        }
        break;

      case M_SHIFT:
        if (i == POT_VALUE) {
          uint8_t s = (uint8_t)(((uint32_t)f * N_SCALES) >> 10);
          if (s >= N_SCALES) s = N_SCALES - 1;
          if (s != scaleIdx) {
            // Зоны 76 скейлов узкие — принимаем только глубже 3 отсчётов
            // от границы, чтобы шум АЦП не листал скейлы сам
            uint16_t lo = (uint16_t)(((uint32_t)s << 10) / N_SCALES);
            uint16_t hi =
                (uint16_t)(((uint32_t)(s + 1) << 10) / N_SCALES);
            if (f >= lo + 3 && f + 3 < hi) {
              setScale(s);
              if (!swapLayers) latchNoteAssignmentPots();
              markSettingsDirty();
              setOverlay(curScale.name);
            }
          }
        } else if (swapLayers) {
          potSendCC(i, f);
        } else if (potToKey[i] >= 0) {
          assignKeyNote((uint8_t)potToKey[i], f);
        }
        break;

      case M_SETUP:
        if (i == POT_VALUE) {
          uint8_t c = (uint8_t)(f >> 6);
          if (c != midiCh) {
            midiCh = c;
            markSettingsDirty();
            *putU(putStr(b, "CHANNEL "), (uint16_t)(midiCh + 1)) = 0;
            setOverlay(b);
          }
        } else {
          uint8_t cc = (uint8_t)(f >> 3);
          if (cc != potCCnum[i]) {
            potCCnum[i] = cc;
            markSettingsDirty();
            char *p = putStr(b, "POT");
            p = putU(p, (uint16_t)(i + 1));
            p = putStr(p, " > CC");
            p = putU(p, cc);
            *p = 0;
            setOverlay(b);
          }
        }
        break;

      case M_ROOT:
        if (i == POT_VALUE) {                    // VALUE-пот = яркость OLED
          uint8_t bv = (uint8_t)(f >> 2);        // 0..255
          if (bv < OLED_BRIGHTNESS_MIN) bv = OLED_BRIGHTNESS_MIN;
          if (bv != oledBrightness) {
            oledBrightness = bv;
            applyBrightness();
            markSettingsDirty();
            *putU(putStr(b, "BRIGHT "), oledBrightness) = 0;
            setOverlay(b);
          }
        } else if (i == 0) {                      // POT1 = тоника (перенесена с VALUE)
          uint8_t r = (uint8_t)(((uint32_t)f * 12) >> 10);
          if (r >= 12) r = 11;
          if (r != rootPC) {
            rootPC = r;
            setScale(scaleIdx);                  // пересчёт лестницы
            if (!swapLayers) latchNoteAssignmentPots();
            markSettingsDirty();
            *putStr(putStr(b, "ROOT "), NOTE_NAMES[rootPC]) = 0;
            setOverlay(b);
          }
        }
        break;
    }
  }
}

// ================== дисплей ==================

// клавиатура: 7 белых клавиш по 9px (x=0..63), высота 23px
const int8_t PC_WHITE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
const int8_t PC_GAP[12]   = {-1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1};

void drawPiano(uint16_t pcMask) {
  for (uint8_t w = 0; w < 7; w++)
    display.drawRect(w * 9, 0, 10, 23, SSD1306_WHITE);

  for (uint8_t pc = 0; pc < 12; pc++) {        // нажатые белые
    if (PC_WHITE[pc] >= 0 && (pcMask & (1u << pc)))
      display.fillRect(PC_WHITE[pc] * 9 + 2, 14, 6, 7, SSD1306_WHITE);
  }
  for (uint8_t pc = 0; pc < 12; pc++) {        // чёрные поверх
    if (PC_GAP[pc] < 0) continue;
    uint8_t x = (uint8_t)(PC_GAP[pc] * 9 + 6);
    display.fillRect(x, 0, 7, 13, SSD1306_BLACK);
    display.drawRect(x, 0, 7, 13, SSD1306_WHITE);
    if (pcMask & (1u << pc))
      display.fillRect(x + 2, 2, 3, 9, SSD1306_WHITE);
  }
  // маркер тоники
  if (PC_WHITE[rootPC] >= 0)
    display.fillRect(PC_WHITE[rootPC] * 9 + 4, 19, 2, 2, SSD1306_WHITE);
  else
    display.fillRect(PC_GAP[rootPC] * 9 + 9, 10, 2, 2, SSD1306_WHITE);
}

void drawScreen() {
  display.clearDisplay();

  uint16_t pcMask = 0;
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    for (uint8_t v = 0; v < CHORD_VOICES; v++) {
      if (sounding[j].note[v] != NO_NOTE) pcMask |= 1u << (sounding[j].note[v] % 12);
    }
  }
#if ENABLE_NRF_RX
  for (uint8_t i = 0; i < NRF_BUTTON_COUNT; i++) {
    for (uint8_t v = 0; v < CHORD_VOICES; v++) {
      if (radioNotes[i].note[v] != NO_NOTE)
        pcMask |= 1u << (radioNotes[i].note[v] % 12);
    }
  }
#endif
  if (previewNote >= 0) pcMask |= 1u << (previewNote % 12);

  drawPiano(pcMask);

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(68, 0);
  display.print(curScale.name);
  display.setCursor(68, 8);
  display.print(NOTE_NAMES[rootPC]);
  display.print(F(" O:"));
  if (octOffset >= 0) display.print('+');
  display.print(octOffset);
  display.setCursor(68, 16);
  display.print(F("CH"));
  display.print(midiCh + 1);
  if (mode != M_PLAY) {
    display.setCursor(98, 16);
    display.print(mode == M_SHIFT ? F("SHIFT") :
                  mode == M_SETUP ? F("SETUP") : F("ROOT"));
  } else if (swapLayers) {
    display.setCursor(98, 16);
    display.print(F("NOTES"));                   // поты сейчас крутят ноты!
  }

  display.setCursor(0, 25);                    // нижняя строка
  display.print(overlayUntil ? overlay : lastEvent);

  display.display();
  if (Wire.getWireTimeoutFlag()) {
    // Залипшая I2C-линия больше не останавливает MIDI навсегда. Wire timeout
    // прерывает транзакцию, после чего отключаем дальнейшие refresh OLED.
    Wire.clearWireTimeoutFlag();
    hasOled = false;
  }
}

void applyBrightness() {
  if (!hasOled) return;
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(oledBrightness);
}

void displayTask() {
  if (overlayUntil && (int32_t)(millis() - overlayUntil) >= 0) {
    overlayUntil = 0;
    previewNote = -1;
    dispDirty = true;
  }
  if (!hasOled || !dispDirty) return;
  if (millis() - dispLastDraw < 40) return;
  dispLastDraw = millis();
  dispDirty = false;
  drawScreen();
}

void ledTask() {
  if (PIN_LED >= 0 && ledOffAt &&
      (int32_t)(millis() - ledOffAt) >= 0) {
    digitalWrite(PIN_LED, LOW);
    ledOffAt = 0;
  }
}

// ================== инициализация ==================

void setup() {
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  digitalWrite(MUX_S0, LOW);
  digitalWrite(MUX_S1, LOW);
  digitalWrite(MUX_S2, LOW);
  for (uint8_t z = 0; z < N_MUX; z++) pinMode(MUX_Z[z], INPUT);
  for (uint8_t i = 0; i < N_KEYS; i++)
    if (KEYS[i].src == 0xFE)
      pinMode(KEYS[i].chan, KEYS[i].activeLow ? INPUT_PULLUP : INPUT);
  if (PIN_LED >= 0) {
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
  }

  // У запаянного nRF CE не должен плавать HIGH, а CSN — случайно выбирать
  // SPI-устройство даже в сборке без RX или до вызова radio.begin().
  pinMode(NRF_CE_PIN, OUTPUT);
  digitalWrite(NRF_CE_PIN, LOW);
  pinMode(NRF_CSN_PIN, OUTPUT);
  digitalWrite(NRF_CSN_PIN, HIGH);

  Serial1.begin(31250);                        // DIN/TRS MIDI OUT

  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    for (uint8_t v = 0; v < CHORD_VOICES; v++) sounding[j].note[v] = NO_NOTE;
    sounding[j].channel = 0;
  }
  for (uint8_t i = 0; i < N_POTS; i++) {
    potToKey[i] = -1;
  }
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    potToKey[KEY_POT[j]] = (int8_t)j;
  }

  // Defaults применяются внутри loadSettings(); валидный блок TMD1 затем
  // заменяет их. Ни byte 0..9, ни официальные radio IDs здесь не пишутся.
  loadSettings();

#if ENABLE_NRF_RX
  setupRadioReceiver();
#endif

  Wire.begin();
  Wire.setWireTimeout(OLED_WIRE_TIMEOUT_US, true);
  Wire.setClock(400000UL);
  hasOled = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, true, false);
  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    hasOled = false;
  }
  if (hasOled) {
    // Встроенный Adafruit splash отключён build flag и не занимает flash.
    // Первый полезный экран нарисует displayTask(); искусственной паузы
    // 800 ms больше нет, поэтому радио и кнопки готовы практически сразу.
    display.clearDisplay();
    applyBrightness();          // восстановить сохранённую яркость
  }

  // первичное чтение — без пачки событий на старте
  for (uint8_t i = 0; i < N_POTS; i++) {
    uint16_t raw = readNode(POTS[i].src, POTS[i].chan);
    potFilt[i] = raw;
    potSent7[i] = (uint8_t)(raw >> 3);
    potSentRaw[i] = raw;
  }
  latchPots();
  for (uint8_t i = 0; i < N_POTS; i++) {
    // Обычный CC-layer можно включить сразу: last-sent уже равен текущему
    // ADC и стартовой пачки CC не будет. Если из EEPROM загружен NOTES-layer,
    // note-поты остаются защёлкнутыми и не сотрут сохранённые назначения.
    potArmed[i] = !swapLayers && i != POT_VALUE;
  }
  potArmed[POT_VALUE] = false;  // сохранённая velocity ждёт реального движения
  for (uint8_t i = 0; i < N_KEYS; i++) {
    keyState[i] = keyRead[i] = false;
    keyT[i] = 0;
  }
}

void loop() {
#if ENABLE_NRF_RX
  radioTask();
#endif

  // Дебаунс сначала фиксирует единый снимок всех кнопок. Только после него
  // вычисляется SHIFT/SETUP/ROOT, и уже затем рассылаются edges. Так результат
  // одновременного SHIFT+кнопка не зависит от номера канала мультиплексора.
  const uint16_t changedKeys = scanKeys();
  updateMode();
  dispatchKeyChanges(changedKeys);
  updatePots();
  if (usbDirty) {
    MidiUSB.flush();
    usbDirty = false;
  }
  displayTask();
  ledTask();
  settingsTask();
}
