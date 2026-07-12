/*
 * TOAST — кастомная прошивка v0.3 «harmony engine»
 * SpaceMelodyLab TOAST v1.1 (Arduino Pro Micro / ATmega32U4)
 *
 * Управление:
 *   8 игровых кнопок       — ноты (полифония, аккорды работают)
 *   KEY11 / KEY1 tap       — period вниз / вверх
 *   KEY11 / KEY1 + PAD     — profile / progression chord
 *   обе октавы + PAD       — P/R/L harmony
 *   KEY4 (SHIFT) + POT9    — выбор скейла
 *   POT1..8                — нота закреплённой кнопки (внутри скейла)
 *   KEY4 + POT1..8         — MIDI CC (слои можно поменять местами)
 *   KEY4 + KEY11 + POT9    — MIDI-канал
 *   KEY4 + KEY11 + POT1..8 — CC-номер пота
 *   KEY4 + KEY1  + POT9    — яркость OLED
 *   KEY4 + KEY1  + POT1    — тоника (root) скейла
 *   KEY4 + KEY1 + POT2..8  — параметры harmony engine
 *
 * Все события идут в USB-MIDI и в DIN/TRS MIDI OUT одновременно.
 * OLED: клавиатура одной октавы + статус + подсказки действий.
 * Штатная радиопанель принимается через nRF24L01+ (это не BLE).
 * Runtime-настройки по умолчанию живут только до перезагрузки; официальные
 * radio IDs в EEPROM прошивка по-прежнему только читает.
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <MIDIUSB.h>
#include <USBAPI.h>
#include <util/atomic.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

#if ENABLE_NRF_RX
#include <SPI.h>
bool nrfOk = false;
#endif

Adafruit_SSD1306 display(128, OLED_HEIGHT, &Wire, -1);
bool hasOled = false;
constexpr uint8_t OLED_WIDTH_PX = 128;
constexpr uint8_t OLED_DATA_PER_TX = BUFFER_LENGTH - 1;
constexpr uint32_t OLED_I2C_HZ = 400000UL;
static_assert(OLED_HEIGHT == 32 && OLED_PAGE_COUNT == 4,
              "OLED page order expects 128x32");
static_assert(BUFFER_LENGTH >= 8, "unexpectedly small Wire buffer");
// 0..3 = transfer slot; 4 = idle. Порядок страниц 3,0,1,2 — status first.
uint8_t oledTxSlot = OLED_PAGE_COUNT;

// ================== скейлы ==================

struct ScaleDef {
  uint8_t len;
  uint8_t period;                   // 12 = октава; 19 = тритава Болен-Пирса
  uint8_t deg[13];
  char name[11];
};

constexpr uint8_t N_SCALES = 76;
constexpr uint8_t SCALE_BP_LAMBDA = 49;
constexpr uint8_t SCALE_BP_CHROMA = 50;

// Для обычных скейлов bit N означает наличие полутона N. Нулевые маски
// зарезервированы за двумя BP-скейлами, у которых период равен 19.
const uint16_t SCALE_MASKS[N_SCALES] PROGMEM = {
  0x0FFF, 0x0AB5, 0x05AD, 0x09AD, 0x0AAD, 0x06AD, 0x05AB, 0x0AD5,
  0x06B5, 0x056B, 0x0295, 0x04A9, 0x04E9, 0x029D, 0x0555, 0x0B6D,
  0x06DB, 0x05B3, 0x09CD, 0x09B3, 0x018D, 0x04A3, 0x0463, 0x02A5,
  0x09B5, 0x06D5, 0x055B, 0x056D, 0x05B5, 0x06AB, 0x0EB5, 0x0BB5,
  0x09AB, 0x0AAB, 0x0D53, 0x0655, 0x0999, 0x04D3, 0x0973, 0x06D9,
  0x06CD, 0x04A5, 0x028D, 0x018B, 0x0DDD, 0x09E7, 0x08E3, 0x0D75,
  0x0BEF, 0x0000, 0x0000, 0x0DD5, 0x0BBB, 0x08E9, 0x06E9, 0x029B,
  0x04ED, 0x06ED, 0x04EB, 0x03A7, 0x0D39, 0x035B, 0x09ED, 0x0673,
  0x0D73, 0x0D4B, 0x06B3, 0x052B, 0x09D3, 0x0B65, 0x05BB, 0x0D6D,
  0x0AB3, 0x06AF, 0x0BAD, 0x0DAD,
};

const uint8_t BP_LAMBDA_DEGREES[9] PROGMEM =
    {0, 3, 4, 7, 9, 12, 13, 16, 18};
const uint8_t BP_CHROMA_DEGREES[13] PROGMEM =
    {0, 1, 3, 4, 6, 7, 9, 10, 12, 13, 15, 16, 18};

// Null-packed вместо [76][11]: полный текст имён без 110 padding bytes.
// Scale меняется редко, поэтому линейный проход по flash дешевле offsets table.
const char SCALE_NAMES[] PROGMEM =
  "CHROMATIC\0MAJOR\0MINOR\0HARM MINOR\0MEL MINOR\0DORIAN\0"
  "PHRYGIAN\0LYDIAN\0MIXOLYDIAN\0LOCRIAN\0PENTA MAJ\0PENTA MIN\0"
  "BLUES MIN\0BLUES MAJ\0WHOLE TONE\0DIM W-H\0DIM H-W\0PHRYG DOM\0"
  "HUNGAR MIN\0DBL HARMON\0HIRAJOSHI\0IN SEN\0IWATO\0YO\0"
  "HARM MAJOR\0LYDIAN DOM\0ALTERED\0LOCRIAN #2\0MIXOLYD b6\0"
  "DORIAN b2\0BEBOP DOM\0BEBOP MAJ\0NEAPOL MIN\0NEAPOL MAJ\0"
  "ENIGMATIC\0PROMETHEUS\0AUGMENTED\0TRITONE\0PERSIAN\0HUNGAR MAJ\0"
  "ROMANIAN\0EGYPTIAN\0KUMOI\0PELOG\0MESSIAEN 3\0MESSIAEN 4\0"
  "MESSIAEN 5\0MESSIAEN 6\0MESSIAEN 7\0BP LAMBDA\0BP CHROMA\0"
  "HARMONICS\0TCHEREPNIN\0BLUES MM7\0BLUES HEPT\0BLUES DHEX\0"
  "BLUES MOD\0BLUES OCT\0BLUES PHRY\0CHROM DOR\0CHROM PHRY\0"
  "ULTRA LOCR\0ALGERIAN\0ORIENTAL\0ENIGMA 8\0ENIGMA MIN\0BHAIRAV\0"
  "RITSU\0PURAVI bVI\0NOHKAN\0FLAMENCO\0SCHWARZ 42\0BHAIRUBAHR\0"
  "ADONAI MLK\0ZIRAFKEND\0UTIL MINOR";

// ================== harmony data ==================

enum ChordQuality : uint8_t {
  Q_MAJ, Q_MIN, Q_DOM7, Q_MAJ7, Q_MIN7, Q_DIM, Q_HDIM7, Q_AUG
};
enum ChordSpread : uint8_t { SP_CLOSE, SP_WIDE, SP_DROP2, SP_DROP3 };

constexpr uint8_t CHORD_PROFILE_COUNT = 6;
constexpr uint8_t CHORD_BANK_COUNT = 8;
constexpr uint8_t CHORD_SKIP = 0xFF;
constexpr uint8_t NEO_INVALID = 0xFF;

const uint8_t CHORD_PROFILES[CHORD_PROFILE_COUNT][CHORD_VOICES] PROGMEM = {
  {0, 4, 6, 9, 11, 15},                       // Wide 9
  {0, 4, 7, 9, 11, 14},                       // Open
  {0, 2, 4, 6, 8, 12},                        // 13 no 11
  {0, 2, 6, 8, CHORD_SKIP, CHORD_SKIP},       // Shell 9
  {0, 3, 6, 9, 12, 15},                       // Quartal
  {0, 4, 8, 12, 16, 20},                      // Quintal
};

const uint16_t QUALITY_MASKS[8] PROGMEM = {
  0x091, 0x089, 0x491, 0x891, 0x489, 0x049, 0x449, 0x111
};

#define CHORD_DESC(rootOffset, quality) \
  (uint8_t)(((uint8_t)(quality) << 4) | (uint8_t)(rootOffset))
const uint8_t PROGRESSION_BANKS[CHORD_BANK_COUNT][N_NOTE_KEYS] PROGMEM = {
  {CHORD_DESC(0,Q_MAJ), CHORD_DESC(7,Q_MAJ), CHORD_DESC(9,Q_MIN), CHORD_DESC(5,Q_MAJ),
   CHORD_DESC(0,Q_MAJ), CHORD_DESC(7,Q_MAJ), CHORD_DESC(9,Q_MIN), CHORD_DESC(5,Q_MAJ)},
  {CHORD_DESC(2,Q_MIN7), CHORD_DESC(7,Q_DOM7), CHORD_DESC(0,Q_MAJ7), CHORD_DESC(9,Q_MIN7),
   CHORD_DESC(2,Q_MIN7), CHORD_DESC(7,Q_DOM7), CHORD_DESC(0,Q_MAJ7), CHORD_DESC(9,Q_MIN7)},
  {CHORD_DESC(0,Q_MAJ), CHORD_DESC(5,Q_MAJ), CHORD_DESC(5,Q_MIN), CHORD_DESC(0,Q_MAJ),
   CHORD_DESC(0,Q_MAJ), CHORD_DESC(5,Q_MAJ), CHORD_DESC(5,Q_MIN), CHORD_DESC(0,Q_MAJ)},
  {CHORD_DESC(10,Q_MAJ), CHORD_DESC(5,Q_MAJ), CHORD_DESC(0,Q_MAJ), CHORD_DESC(10,Q_MAJ),
   CHORD_DESC(5,Q_MAJ), CHORD_DESC(0,Q_MAJ), CHORD_DESC(10,Q_MAJ), CHORD_DESC(0,Q_MAJ)},
  {CHORD_DESC(0,Q_MIN), CHORD_DESC(10,Q_MAJ), CHORD_DESC(8,Q_MAJ), CHORD_DESC(10,Q_MAJ),
   CHORD_DESC(0,Q_MIN), CHORD_DESC(10,Q_MAJ), CHORD_DESC(8,Q_MAJ), CHORD_DESC(10,Q_MAJ)},
  {CHORD_DESC(8,Q_MAJ), CHORD_DESC(10,Q_MAJ), CHORD_DESC(0,Q_MAJ), CHORD_DESC(8,Q_MAJ),
   CHORD_DESC(10,Q_MAJ), CHORD_DESC(0,Q_MAJ), CHORD_DESC(10,Q_MAJ), CHORD_DESC(0,Q_MAJ)},
  {CHORD_DESC(0,Q_MIN), CHORD_DESC(5,Q_MAJ), CHORD_DESC(0,Q_MIN), CHORD_DESC(5,Q_MAJ),
   CHORD_DESC(0,Q_MIN), CHORD_DESC(5,Q_MAJ), CHORD_DESC(0,Q_MIN), CHORD_DESC(5,Q_MAJ)},
  {CHORD_DESC(0,Q_MAJ), CHORD_DESC(4,Q_MAJ), CHORD_DESC(8,Q_MAJ), CHORD_DESC(0,Q_MAJ),
   CHORD_DESC(0,Q_MAJ), CHORD_DESC(4,Q_MAJ), CHORD_DESC(8,Q_MAJ), CHORD_DESC(0,Q_MAJ)},
};
#undef CHORD_DESC

// Low nibble first, high nibble second. 0=P, 1=R, 2=L, 0xF=stop.
const uint8_t PLR_SEQUENCES[7] PROGMEM =
    {0xF0, 0xF1, 0xF2, 0x10, 0x20, 0x01, 0x21};

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
int8_t   ladderMin = 0;         // первый индекс лестницы, дающий MIDI note >= 0
int8_t   ladderMax = 127;       // последний индекс лестницы, дающий note <= 127
int8_t   keyLadder[N_NOTE_KEYS]; // вычисленный индекс ступени текущего скейла
// Каноническое намерение пользователя: высота кнопки без octOffset,
// относительно rootPC. При смене скейла эти anchors не меняются, поэтому
// накрученные индивидуальные октавы точно возвращаются вместе со скейлом.
int8_t   keyRelSemitone[N_NOTE_KEYS];
uint8_t  potCCnum[N_POTS];       // CC-номер каждого пота

Mode     mode = M_PLAY;
bool     swapLayers = true;     // true: поты без шифта крутят ноты, CC — под шифтом
bool     comboLatched = false;  // защёлка комбо SHIFT+обе октавы
int8_t   previewNote = -1;      // нота, выбираемая потом — подсветка на пиано
uint8_t  velocity = KEY_VELOCITY;  // velocity нот, крутится value-потом в игре
uint8_t  oledBrightness = OLED_BRIGHTNESS_DEFAULT;  // контраст SSD1306 0..255

uint8_t  chordProfile = 0;
uint8_t  chordInversion = 0;
uint8_t  chordVoiceCount = CHORD_VOICES;
uint8_t  chordSpread = SP_CLOSE;
uint8_t  chordBank = 0;
int8_t   chordRegister = 0;
bool     chordVoiceLeading = true;
uint8_t  previousVoicing[CHORD_VOICES];
uint8_t  previousVoiceCount = 0;
// bit7 = minor, bits0..6 = MIDI root 24..95.
uint8_t  neoState = NEO_INVALID;

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
// [0] = OCT-down/profile, [1] = OCT-up/progression; вместе = P/R/L.
bool octConsumed[2] = {false, false};
uint8_t dispatchOctMask = 0;      // включает OCT, отпущенные в том же scan

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

bool     dispDirty = true;
uint32_t ledOffAt = 0, dispLastDraw = 0, overlayUntil = 0;
char     overlay[22] = "";
char     lastEvent[22] = "TOAST v0.3";

// ================== опциональное EEPROM ==================

#if ENABLE_SETTINGS_PERSISTENCE
// На little-endian AVR эти четыре байта лежат как ASCII "TMD1".
constexpr uint32_t SETTINGS_MAGIC = 0x31444D54UL;
constexpr uint8_t SETTINGS_VERSION = 3;  // v3: relative anchors + int8 cache
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
  int8_t keyRel[N_NOTE_KEYS];
  int8_t keyStep[N_NOTE_KEYS];
  uint16_t crc;
};

StoredSettings pendingSettings;
bool settingsDirty = false;
bool settingsWriteActive = false;
uint8_t settingsWritePos = 0;
uint32_t settingsChangedAt = 0;

static_assert(sizeof(StoredSettings) <= 255, "EEPROM writer uses an 8-bit index");
static_assert(SETTINGS_EEPROM_START >= 10, "first ten EEPROM bytes are reserved");
static_assert(SETTINGS_EEPROM_START + sizeof(StoredSettings) <=
                  NRF_EEPROM_KEYPAD_1,
              "custom settings must not overlap official keypad IDs");
#endif

static_assert(N_KEYS <= 16, "key change mask is 16 bit");
static_assert(N_NOTE_KEYS == 8, "UI and radio mapping expect eight note keys");
static_assert(POT_VALUE < N_POTS, "POT_VALUE must index POT_NODES");

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
  int16_t n = ladderNote(keyLadder[j]) + curScale.period * octOffset;
  while (n > 127) n -= curScale.period;
  while (n < 0) n += curScale.period;
  return (uint8_t)n;
}

void resetLadder() {                          // кнопки = первые 8 ступеней от ~C3
  int16_t base = curScale.len * (60 / curScale.period);
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    keyLadder[j] = (int8_t)(base + j);
    keyRelSemitone[j] = (int8_t)(ladderNote(keyLadder[j]) - rootPC);
  }
}

void updateLadderBounds() {
  int16_t k = 0;
  while (ladderNote(k) >= 0) k--;
  ladderMin = (int8_t)(k + 1);
  k = 0;
  while (ladderNote(k + 1) <= 127) k++;
  ladderMax = (int8_t)k;
}

void loadScale(uint8_t s) {
  scaleIdx = s;
  memset(&curScale, 0, sizeof(curScale));
  PGM_P name = SCALE_NAMES;
  for (uint8_t i = 0; i < s; i++) {
    while (pgm_read_byte(name++)) {}
  }
  for (uint8_t i = 0; i < sizeof(curScale.name) - 1; i++) {
    const char c = (char)pgm_read_byte(name++);
    curScale.name[i] = c;
    if (!c) break;
  }
  if (s == SCALE_BP_LAMBDA || s == SCALE_BP_CHROMA) {
    const bool chroma = s == SCALE_BP_CHROMA;
    curScale.len = chroma ? 13 : 9;
    curScale.period = 19;
    memcpy_P(curScale.deg,
             chroma ? BP_CHROMA_DEGREES : BP_LAMBDA_DEGREES,
             curScale.len);
  } else {
    curScale.period = 12;
    const uint16_t mask = pgm_read_word(SCALE_MASKS + s);
    for (uint8_t pc = 0; pc < 12; pc++) {
      if (mask & ((uint16_t)1 << pc)) curScale.deg[curScale.len++] = pc;
    }
  }
  updateLadderBounds();
}

// Найти ближайшую доступную ступень к сохранённой высоте. При точной ничьей
// сохраняем фазу старой лестницы: Eb minor -> E major, а не D major.
int8_t resolveLadder(int16_t target, int8_t oldK, uint8_t oldLen) {
  int16_t lo = ladderMin, hi = ladderMax;
  while (lo < hi) {
    const int16_t mid = (lo + hi) / 2;
    if (ladderNote(mid) < target) lo = mid + 1;
    else hi = mid;
  }
  const int8_t upper = (int8_t)lo;
  if (upper == ladderMin) return upper;
  const int8_t lower = (int8_t)(upper - 1);
  int16_t lowerDistance = target - ladderNote(lower);
  if (lowerDistance < 0) lowerDistance = -lowerDistance;
  int16_t upperDistance = ladderNote(upper) - target;
  if (upperDistance < 0) upperDistance = -upperDistance;
  if (lowerDistance != upperDistance) {
    return lowerDistance < upperDistance ? lower : upper;
  }
  int16_t lowerPhase = (int16_t)(lower * oldLen - oldK * curScale.len);
  int16_t upperPhase = (int16_t)(upper * oldLen - oldK * curScale.len);
  if (lowerPhase < 0) lowerPhase = -lowerPhase;
  if (upperPhase < 0) upperPhase = -upperPhase;
  return lowerPhase <= upperPhase ? lower : upper;
}

void resolveKeyLayout(const int8_t *oldSteps, uint8_t oldLen) {
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    const int16_t target = rootPC + keyRelSemitone[j];
    keyLadder[j] = resolveLadder(target, oldSteps[j], oldLen);
  }
}

void changeScale(uint8_t s) {
  int8_t oldSteps[N_NOTE_KEYS];
  memcpy(oldSteps, keyLadder, sizeof(oldSteps));
  const uint8_t oldLen = curScale.len;
  loadScale(s);
  resolveKeyLayout(oldSteps, oldLen);
  neoState = NEO_INVALID;
}

void changeRoot(uint8_t r) {
  int8_t oldSteps[N_NOTE_KEYS];
  memcpy(oldSteps, keyLadder, sizeof(oldSteps));
  rootPC = r;
  updateLadderBounds();
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    int16_t step = oldSteps[j];
    int16_t relative = ladderNote(step) - rootPC;
    const uint8_t halfPeriod = curScale.period / 2;
    while (relative + halfPeriod < keyRelSemitone[j]) {
      step += curScale.len;
      relative += curScale.period;
    }
    while (relative - halfPeriod > keyRelSemitone[j]) {
      step -= curScale.len;
      relative -= curScale.period;
    }
    while (step < ladderMin) step += curScale.len;
    while (step > ladderMax) step -= curScale.len;
    keyLadder[j] = (int8_t)step;
  }
  neoState = NEO_INVALID;
}

void setOverlay(const char *s) {
  strncpy(overlay, s, sizeof(overlay) - 1);
  overlay[sizeof(overlay) - 1] = 0;
  overlayUntil = millis() + OVERLAY_MS;
  previewNote = -1;             // каждая новая подсказка сама решает, что подсвечивать
  dispDirty = true;
}

uint8_t potZone(uint16_t value, uint8_t zones) {
  uint8_t zone = (uint8_t)(((uint32_t)value * zones) >> 10);
  return zone < zones ? zone : (uint8_t)(zones - 1);
}

bool heldEmpty(const HeldNotes &h) {
  for (uint8_t v = 0; v < CHORD_VOICES; v++) {
    if (h.note[v] != NO_NOTE) return false;
  }
  return true;
}

#if ENABLE_SETTINGS_PERSISTENCE
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
#endif

void setDefaultSettings() {
  rootPC = 0;
  octOffset = 0;
  midiCh = MIDI_CH;
  velocity = KEY_VELOCITY;
  swapLayers = true;
  oledBrightness = OLED_BRIGHTNESS_DEFAULT;
  chordProfile = 0;
  chordInversion = 0;
  chordVoiceCount = CHORD_VOICES;
  chordSpread = SP_CLOSE;
  chordBank = 0;
  chordRegister = 0;
  chordVoiceLeading = true;
  previousVoiceCount = 0;
  neoState = NEO_INVALID;
  loadScale(0);
  resetLadder();
  for (uint8_t i = 0; i < N_POTS; i++) {
    potCCnum[i] = (i == POT_VALUE) ? 0 : (uint8_t)(i + 1);
  }
}

#if ENABLE_SETTINGS_PERSISTENCE
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
  loadScale(saved.scale);
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    if (saved.keyRel[j] < -11 ||
        saved.keyStep[j] < ladderMin || saved.keyStep[j] > ladderMax) {
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
  for (uint8_t j = 0; j < N_NOTE_KEYS; j++) {
    keyRelSemitone[j] = saved.keyRel[j];
    keyLadder[j] = saved.keyStep[j];
  }
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
    pendingSettings.keyRel[j] = keyRelSemitone[j];
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
#else
void loadSettings() { setDefaultSettings(); }
void markSettingsDirty() {}
void settingsTask() {}
#endif

// ================== чтение входов ==================

uint16_t readNode(uint8_t src, uint8_t chan) {
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
  const uint8_t pin = MUX_Z[src];
  analogRead(pin);
  return (uint16_t)analogRead(pin);
}

uint16_t readPackedNode(const uint8_t *nodes, uint8_t index) {
  const uint8_t node = pgm_read_byte(nodes + index);
  return readNode(NODE_SRC(node), NODE_CHAN(node));
}

uint8_t potForKey(uint8_t key) {
  return (uint8_t)((key >> 1) + ((key & 1) ? 0 : 4));
}

int8_t keyForPot(uint8_t pot) {
  if (pot >= N_NOTE_KEYS) return -1;
  return (pot < 4) ? (int8_t)(pot * 2 + 1)
                   : (int8_t)((pot - 4) * 2);
}

// ================== отправка MIDI ==================

namespace UsbMidiTx {
constexpr uint8_t TX_EP = CDC_FIRST_ENDPOINT + CDC_ENPOINT_COUNT + 1;
constexpr uint8_t Q_CAP = 16;
constexpr uint8_t Q_MASK = Q_CAP - 1;
static_assert(TX_EP == 5, "MIDIUSB endpoint mapping changed");
static_assert(TX_EP < USB_ENDPOINTS, "not enough AVR USB endpoints");
static_assert((Q_CAP & Q_MASK) == 0, "USB MIDI queue must be power of two");
static_assert(sizeof(midiEventPacket_t) == 4, "unexpected USB MIDI packet");

midiEventPacket_t queue[Q_CAP];
uint8_t head = 0, tail = 0, count = 0;
bool desynced = false;
uint8_t panicStep = 0;  // CC120 по одному разу на каждый MIDI channel

void task();

void flushNonblocking() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { MidiUSB.flush(); }
}

void clearQueue() { head = tail = count = 0; }

void enterRecovery() {
  clearQueue();
  desynced = true;
  panicStep = 0;
}

bool enqueue(const midiEventPacket_t &packet, bool replacePendingCc) {
  if (!USBDevice.configured() || desynced) return false;

  if (replacePendingCc) {
    for (uint8_t i = 0; i < count; i++) {
      midiEventPacket_t &old = queue[(tail + i) & Q_MASK];
      if (old.byte1 == packet.byte1 && old.byte2 == packet.byte2) {
        old = packet;
        return true;
      }
    }
  }

  if (count == Q_CAP) task();
  if (!USBDevice.configured() || desynced) return false;
  if (count == Q_CAP) {
    // После восстановления host снимем все потенциально зависшие ноты.
    enterRecovery();
    return false;
  }
  queue[head] = packet;
  head = (head + 1) & Q_MASK;
  count++;
  return true;
}

void enqueueNote(uint8_t channel, uint8_t note, bool on, uint8_t noteVelocity) {
  const uint8_t status = (uint8_t)((on ? 0x90 : 0x80) | (channel & 0x0F));
  const midiEventPacket_t packet = {
    (uint8_t)(on ? 0x09 : 0x08), status, note,
    (uint8_t)(on ? noteVelocity : 0)
  };
  enqueue(packet, false);
}

void enqueueCC(uint8_t channel, uint8_t cc, uint8_t value, bool continuous) {
  const uint8_t status = (uint8_t)(0xB0 | (channel & 0x0F));
  const midiEventPacket_t packet = {0x0B, status, cc, value};
  enqueue(packet, continuous);
}

void task() {
  if (!USBDevice.configured()) {
    clearQueue();
    desynced = false;
    panicStep = 0;
    return;
  }

  uint8_t space = USB_SendSpace(TX_EP);
  if (space && space < 8) {
    flushNonblocking();
  }

  bool wrote = false;
  uint8_t budget = 15;  // максимум 60 из 64 bytes: без ZLP/wait path
  if (desynced) {
    while (panicStep < 16 && budget && USB_SendSpace(TX_EP) >= 8) {
      const uint8_t channel = panicStep & 0x0F;
      const midiEventPacket_t packet = {
        0x0B, (uint8_t)(0xB0 | channel), 120, 0
      };
      MidiUSB.sendMIDI(packet);
      panicStep++;
      budget--;
      wrote = true;
    }
    if (wrote) flushNonblocking();
    if (panicStep == 16) {
      desynced = false;
      panicStep = 0;
    }
    return;
  }

  while (count && budget && USB_SendSpace(TX_EP) >= 8) {
    MidiUSB.sendMIDI(queue[tail]);
    tail = (tail + 1) & Q_MASK;
    count--;
    budget--;
    wrote = true;
  }
  if (wrote) flushNonblocking();
}
}  // namespace UsbMidiTx

enum LedColor : uint8_t {
  LED_OFF = 0, LED_RED = 1, LED_GREEN = 2, LED_BLUE = 4,
  LED_YELLOW = LED_RED | LED_GREEN,
  LED_MAGENTA = LED_RED | LED_BLUE,
  LED_CYAN = LED_GREEN | LED_BLUE,
  LED_WHITE = LED_RED | LED_GREEN | LED_BLUE
};

uint8_t ledBaseColor = LED_GREEN;

void rgbWrite(uint8_t color) {
  digitalWrite(PIN_LED_R, color & LED_RED ? HIGH : LOW);
  digitalWrite(PIN_LED_G, color & LED_GREEN ? HIGH : LOW);
  digitalWrite(PIN_LED_B, color & LED_BLUE ? HIGH : LOW);
}

void updateLedBase() {
#if ENABLE_NRF_RX
  if (!nrfOk) ledBaseColor = LED_RED;
  else
#endif
  if (mode == M_SETUP) ledBaseColor = LED_MAGENTA;
  else if (mode == M_ROOT) ledBaseColor = LED_BLUE;
  else if (mode == M_SHIFT) ledBaseColor = LED_YELLOW;
  else ledBaseColor = swapLayers ? LED_GREEN : LED_CYAN;
  if (!ledOffAt) rgbWrite(ledBaseColor);
}

void ledFlash() {
  rgbWrite(LED_WHITE);
  ledOffAt = millis() + 25;
}

void dinWrite3(uint8_t a, uint8_t b, uint8_t c) {
  Serial1.write(a);
  Serial1.write(b);
  Serial1.write(c);
}

void sendCCOnChannel(uint8_t channel, uint8_t cc, uint8_t val,
                     bool continuous = false) {
  const uint8_t status = (uint8_t)(0xB0 | channel);
  // Полный DIN event всегда уходит до любого обращения к USB.
  dinWrite3(status, cc, val);
  UsbMidiTx::enqueueCC(channel, cc, val, continuous);
  ledFlash();
  char *q = putStr(lastEvent, "CC");
  q = putU(q, cc);
  q = putStr(q, " = ");
  q = putU(q, val);
  *q = 0;
  dispDirty = true;
}

void sendCC(uint8_t cc, uint8_t val) {
  sendCCOnChannel(midiCh, cc, val, true);
}

void sendNoteBatch(uint8_t channel, const uint8_t *notes, uint8_t count,
                   bool on) {
  const uint8_t status = (uint8_t)((on ? 0x90 : 0x80) | channel);
  const uint8_t noteVelocity = on ? velocity : 0;
  // Критическое правило: сначала ВСЯ пачка DIN, затем только USB queue.
  for (uint8_t i = 0; i < count; i++) {
    dinWrite3(status, notes[i], noteVelocity);
  }
  for (uint8_t i = 0; i < count; i++) {
    UsbMidiTx::enqueueNote(channel, notes[i], on, noteVelocity);
  }
  if (count) {
    ledFlash();
    if (on) fmtNote(lastEvent, notes[0]);
    dispDirty = true;
  }
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
  uint8_t stored = 0;
  for (uint8_t v = 0; v < count && v < CHORD_VOICES; v++) {
    bool duplicate = false;
    for (uint8_t k = 0; k < stored; k++) {
      if (held.note[k] == notes[v]) duplicate = true;
    }
    if (!duplicate) held.note[stored++] = notes[v];
  }
  for (uint8_t v = stored; v < CHORD_VOICES; v++) held.note[v] = NO_NOTE;

  // Note On шлём только для нот, которых не держит другой источник, чтобы
  // повторной высоты не было. ignore=&held: собственные ноты не считаем.
  uint8_t send[CHORD_VOICES];
  uint8_t sendCount = 0;
  for (uint8_t v = 0; v < stored; v++) {
    if (!anotherSourceHolds(held.note[v], held.channel, &held)) {
      send[sendCount++] = held.note[v];
    }
  }
  sendNoteBatch(held.channel, send, sendCount, true);
}

void pressNoteSource(HeldNotes &held, uint8_t note) {
  pressNoteSet(held, &note, 1);
}

void releaseNoteSource(HeldNotes &held) {
  const uint8_t channel = held.channel;
  uint8_t released[CHORD_VOICES];
  uint8_t releasedCount = 0;
  for (uint8_t v = 0; v < CHORD_VOICES; v++) {
    const uint8_t note = held.note[v];
    if (note == NO_NOTE) continue;
    held.note[v] = NO_NOTE;                      // очищаем ДО проверки dedup
    if (!anotherSourceHolds(note, channel, nullptr)) {
      released[releasedCount++] = note;
    }
  }
  sendNoteBatch(channel, released, releasedCount, false);
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
  const uint8_t note = pgm_read_byte(KEY_TO_NOTE + keyIdx);
  return note == 0xFF ? -1 : (int8_t)note;
}

void sortNotes16(int16_t *notes, uint8_t count) {
  for (uint8_t i = 1; i < count; i++) {
    const int16_t value = notes[i];
    uint8_t k = i;
    while (k && notes[k - 1] > value) {
      notes[k] = notes[k - 1];
      k--;
    }
    notes[k] = value;
  }
}

void shiftVoicing(int16_t *notes, uint8_t count, int16_t delta) {
  for (uint8_t i = 0; i < count; i++) notes[i] += delta;
}

// В MIDI range переносится весь voicing целиком. Ни один крайний голос не
// отбрасывается, поэтому Note On/Off и фактура всегда сохраняют размер.
bool fitWholeVoicing(int16_t *notes, uint8_t count, uint8_t period) {
  if (!count || !period) return false;
  sortNotes16(notes, count);
  if (notes[count - 1] - notes[0] > 127) return false;
  while (notes[count - 1] > 127) shiftVoicing(notes, count, -(int16_t)period);
  while (notes[0] < 0) shiftVoicing(notes, count, period);
  return notes[count - 1] <= 127;
}

void rotateLowestVoice(int16_t *notes, uint8_t count, uint8_t period) {
  int16_t moved = notes[0];
  const int16_t top = notes[count - 1];
  do moved += period; while (moved <= top);
  for (uint8_t i = 1; i < count; i++) notes[i - 1] = notes[i];
  notes[count - 1] = moved;
}

bool noteCollides(const int16_t *notes, uint8_t count, uint8_t index) {
  for (uint8_t i = 0; i < count; i++) {
    if (i != index && notes[i] == notes[index]) return true;
  }
  return false;
}

void applyChordSpread(int16_t *notes, uint8_t count, uint8_t period) {
  sortNotes16(notes, count);
  if (chordSpread == SP_WIDE) {
    for (uint8_t i = count / 2; i < count; i++) notes[i] += period;
  } else if (count >= 4 &&
             (chordSpread == SP_DROP2 || chordSpread == SP_DROP3)) {
    const uint8_t index = chordSpread == SP_DROP2 ? count - 2 : count - 3;
    do notes[index] -= period;
    while (noteCollides(notes, count, index));
  }
  sortNotes16(notes, count);
}

uint8_t finishVoicing(int16_t *base, uint8_t count,
                      uint8_t period, uint8_t *out) {
  if (!count || count > CHORD_VOICES || !period) return 0;
  sortNotes16(base, count);
  applyChordSpread(base, count, period);
  if (!fitWholeVoicing(base, count, period)) return 0;
  uint8_t preferred = chordInversion;
  while (preferred >= count) preferred -= count;
  bool found = false;

  if (chordVoiceLeading && previousVoiceCount == count) {
    uint16_t bestMovement = 0xFFFF;
    int16_t candidate[CHORD_VOICES];
    memcpy(candidate, base, count * sizeof(candidate[0]));
    for (uint8_t r = 0; r < preferred; r++) {
      rotateLowestVoice(candidate, count, period);
    }
    for (uint8_t k = 0; k < count; k++) {
      if (!fitWholeVoicing(candidate, count, period)) continue;
      for (int8_t reg = -2; reg <= 2; reg++) {
        const int16_t delta = (int16_t)reg * period;
        if (candidate[0] + delta < 0 ||
            candidate[count - 1] + delta > 127) continue;
        uint16_t movement = 0;
        for (uint8_t v = 0; v < count; v++) {
          const int16_t pitch = candidate[v] + delta;
          int16_t distance = pitch - previousVoicing[v];
          if (distance < 0) distance = -distance;
          movement += (uint16_t)distance;
        }
        if (!found || movement < bestMovement) {
          found = true;
          bestMovement = movement;
          for (uint8_t v = 0; v < count; v++) {
            out[v] = (uint8_t)(candidate[v] + delta);
          }
        }
      }
      rotateLowestVoice(candidate, count, period);
    }
  }

  if (!found) {
    for (uint8_t r = 0; r < preferred; r++) {
      rotateLowestVoice(base, count, period);
    }
    if (!fitWholeVoicing(base, count, period)) return 0;
    for (uint8_t v = 0; v < count; v++) out[v] = (uint8_t)base[v];
  }
  memcpy(previousVoicing, out, count);
  previousVoiceCount = count;
  return count;
}

uint8_t countMaskTones(uint16_t mask) {
  uint8_t count = 0;
  for (uint8_t pc = 0; pc < 12; pc++) {
    if (mask & ((uint16_t)1 << pc)) count++;
  }
  return count;
}

uint8_t maskToneAt(uint16_t mask, uint8_t wanted) {
  for (uint8_t pc = 0; pc < 12; pc++) {
    if (!(mask & ((uint16_t)1 << pc))) continue;
    if (!wanted) return pc;
    wanted--;
  }
  return 0;
}

uint8_t buildScaleProfileChord(uint8_t key, uint8_t *out) {
  const uint8_t profile = chordProfile < CHORD_PROFILE_COUNT ? chordProfile : 0;
  uint8_t limit = chordVoiceCount;
  if (limit < 3) limit = 3;
  if (limit > CHORD_VOICES) limit = CHORD_VOICES;
  const int16_t registerShift =
      (int16_t)curScale.period * (octOffset + chordRegister);
  int16_t work[CHORD_VOICES];
  uint8_t count = 0;
  for (uint8_t slot = 0; slot < CHORD_VOICES && count < limit; slot++) {
    const uint8_t offset = pgm_read_byte(&CHORD_PROFILES[profile][slot]);
    if (offset != CHORD_SKIP) {
      work[count++] = ladderNote(keyLadder[key] + offset) + registerShift;
    }
  }
  return finishVoicing(work, count, curScale.period, out);
}

uint8_t buildQualityChord(int16_t root, uint8_t quality, uint8_t *out) {
  if (quality > Q_AUG) return 0;
  const uint16_t mask = pgm_read_word(&QUALITY_MASKS[quality]);
  const uint8_t toneCount = countMaskTones(mask);
  uint8_t target = chordVoiceCount;
  if (target < 3) target = 3;
  if (target > CHORD_VOICES) target = CHORD_VOICES;
  int16_t work[CHORD_VOICES];
  if (target == 3 && toneCount == 4) {
    work[0] = root + maskToneAt(mask, 0);
    work[1] = root + maskToneAt(mask, 1);
    work[2] = root + maskToneAt(mask, 3);       // root, third, seventh
  } else {
    uint8_t ordinal = 0;
    int16_t octave = 0;
    for (uint8_t v = 0; v < target; v++) {
      work[v] = root + maskToneAt(mask, ordinal) + octave;
      if (++ordinal == toneCount) { ordinal = 0; octave += 12; }
    }
  }
  return finishVoicing(work, target, 12, out);
}

uint8_t foldNeoRoot(int16_t root) {
  while (root < 24) root += 12;
  while (root > 95) root -= 12;
  return (uint8_t)root;
}

bool homeScaleIsMinor() {
  bool minorThird = false, majorThird = false;
  for (uint8_t i = 0; i < curScale.len; i++) {
    if (curScale.deg[i] == 3) minorThird = true;
    if (curScale.deg[i] == 4) majorThird = true;
  }
  return minorThird && !majorThird;
}

void resetNeoState() {
  const int16_t root = 48 + rootPC + curScale.period * octOffset +
                       12 * chordRegister;
  neoState = foldNeoRoot(root);
  if (homeScaleIsMinor()) neoState |= 0x80;
}

void seedNeoFromQuality(int16_t root, uint8_t quality) {
  bool minor;
  if (quality == Q_MIN || quality == Q_MIN7) minor = true;
  else if (quality == Q_MAJ || quality == Q_MAJ7 || quality == Q_DOM7) minor = false;
  else return;
  neoState = foldNeoRoot(root);
  if (minor) neoState |= 0x80;
}

void applyNeoOperation(uint8_t operation) {
  if (neoState == NEO_INVALID) resetNeoState();
  int16_t root = neoState & 0x7F;
  bool minor = neoState & 0x80;
  if (operation == 0) minor = !minor;           // Parallel
  else if (operation == 1) { root += minor ? 3 : -3; minor = !minor; }
  else if (operation == 2) { root += minor ? -4 : 4; minor = !minor; }
  neoState = foldNeoRoot(root);
  if (minor) neoState |= 0x80;
}

uint8_t buildProgressionChord(uint8_t key, uint8_t *out) {
  const uint8_t bank = chordBank < CHORD_BANK_COUNT ? chordBank : 0;
  const uint8_t descriptor = pgm_read_byte(&PROGRESSION_BANKS[bank][key]);
  const uint8_t quality = descriptor >> 4;
  const int16_t root = 48 + rootPC + (descriptor & 0x0F) +
                       curScale.period * octOffset + 12 * chordRegister;
  const uint8_t count = buildQualityChord(root, quality, out);
  if (count) seedNeoFromQuality(root, quality);
  return count;
}

uint8_t buildPlrChord(uint8_t key, uint8_t *out) {
  if (key == 7) resetNeoState();
  else {
    if (neoState == NEO_INVALID) resetNeoState();
    const uint8_t sequence = pgm_read_byte(PLR_SEQUENCES + key);
    applyNeoOperation(sequence & 0x0F);
    const uint8_t second = sequence >> 4;
    if (second < 3) applyNeoOperation(second);
  }
  return buildQualityChord(neoState & 0x7F,
                           neoState & 0x80 ? Q_MIN : Q_MAJ, out);
}

uint8_t buildHarmonyChord(uint8_t key, uint8_t octMask, uint8_t *out) {
  if (octMask == 1) return buildScaleProfileChord(key, out);
  if (octMask == 2) return buildProgressionChord(key, out);
  if (octMask == 3) return buildPlrChord(key, out);
  return 0;
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
        previousVoiceCount = 0;
        neoState = NEO_INVALID;
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
      const uint8_t octMask = dispatchOctMask;
      if (octMask) {
        if (octMask & 1) octConsumed[0] = true;
        if (octMask & 2) octConsumed[1] = true;
        uint8_t chord[CHORD_VOICES];
        const uint8_t n = buildHarmonyChord((uint8_t)j, octMask, chord);
        pressNoteSet(sounding[j], chord, n);
      } else {
        pressNoteSource(sounding[j], playedNote((uint8_t)j));
      }
    } else if (mode == M_SHIFT) {
      if (j == 0) {
        resetLadder();
        previousVoiceCount = 0;
        if (!swapLayers) latchNoteAssignmentPots();
        markSettingsDirty();
        setOverlay("NOTES RESET");
      } else if (j <= CHORD_PROFILE_COUNT) {
        chordProfile = (uint8_t)(j - 1);
        char b[14];
        *putU(putStr(b, "PROFILE "), (uint16_t)(chordProfile + 1)) = 0;
        setOverlay(b);
      } else if (j == 7) {
        chordVoiceLeading = !chordVoiceLeading;
        setOverlay(chordVoiceLeading ? "VOICE LEAD ON" : "VOICE LEAD OFF");
      }
    }
  } else {
    releaseNoteSource(sounding[j]);              // note-off отдаём всегда
  }
}

uint16_t scanKeys() {
  uint16_t changed = 0;
  for (uint8_t i = 0; i < N_KEYS; i++) {
    const bool pressed = readPackedNode(KEY_NODES, i) < 340;
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
  dispatchOctMask =
      (keyState[BTN_OCT_DOWN] ? 1 : 0) |
      (keyState[BTN_OCT_UP] ? 2 : 0);
  // PAD press обрабатывается раньше release-pass. Сохраняем в effective mask
  // модификатор, который завершил debounce-release в том же самом scan.
  if ((changed & ((uint16_t)1 << BTN_OCT_DOWN)) && !keyState[BTN_OCT_DOWN]) {
    dispatchOctMask |= 1;
  }
  if ((changed & ((uint16_t)1 << BTN_OCT_UP)) && !keyState[BTN_OCT_UP]) {
    dispatchOctMask |= 2;
  }

  // Свежее нажатие октавной кнопки обнуляет флаг «потрачено» — но ДО раздачи
  // нот в этом же скане, иначе аккорд (индекс нот-кнопки может быть меньше)
  // затёрся бы поздним обработчиком октавы. Шифт при нажатии = сразу режим.
  if ((changed & ((uint16_t)1 << BTN_OCT_DOWN)) && keyState[BTN_OCT_DOWN]) {
    octConsumed[0] = keyState[BTN_SHIFT];
  }
  if ((changed & ((uint16_t)1 << BTN_OCT_UP)) && keyState[BTN_OCT_UP]) {
    octConsumed[1] = keyState[BTN_SHIFT];
  }
  if (keyState[BTN_OCT_DOWN] && keyState[BTN_OCT_UP]) {
    octConsumed[0] = true;
    octConsumed[1] = true;
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
    const uint8_t i = potForKey(j);
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
    updateLedBase();
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
    updateLedBase();
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
  const int8_t rel = (int8_t)(ladderNote(k) - rootPC);
  if (k == keyLadder[j] && rel == keyRelSemitone[j]) return;
  keyLadder[j] = (int8_t)k;
  keyRelSemitone[j] = rel;
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
    uint16_t raw = readPackedNode(POT_NODES, i);
    // Максимально резкий EMA, но фильтр остаётся: 7/8 нового + 1/8 старого.
    // Реагирует почти мгновенно (за ~1.5 чтения), но одиночные выбросы АЦП
    // всё же пригашиваются. В покое дребезг ловит дедбэнд POT_SEND_THRESHOLD.
    potFilt[i] = (uint16_t)((potFilt[i] + raw * 7) >> 3);
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
          const int8_t j = keyForPot(i);
          if (j >= 0) assignKeyNote((uint8_t)j, f);
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
              changeScale(s);
              if (!swapLayers) latchNoteAssignmentPots();
              markSettingsDirty();
              setOverlay(curScale.name);
            }
          }
        } else if (swapLayers) {
          potSendCC(i, f);
        } else {
          const int8_t j = keyForPot(i);
          if (j >= 0) assignKeyNote((uint8_t)j, f);
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
            changeRoot(r);
            if (!swapLayers) latchNoteAssignmentPots();
            markSettingsDirty();
            *putStr(putStr(b, "ROOT "), NOTE_NAMES[rootPC]) = 0;
            setOverlay(b);
          }
        } else if (i == 1) {                    // POT2 = scale-profile
          const uint8_t value = potZone(f, CHORD_PROFILE_COUNT);
          if (value != chordProfile) {
            chordProfile = value;
            *putU(putStr(b, "PROFILE "), (uint16_t)(chordProfile + 1)) = 0;
            setOverlay(b);
          }
        } else if (i == 2) {                    // POT3 = inversion
          const uint8_t value = potZone(f, CHORD_VOICES);
          if (value != chordInversion) {
            chordInversion = value;
            *putU(putStr(b, "INVERSION "), chordInversion) = 0;
            setOverlay(b);
          }
        } else if (i == 3) {                    // POT4 = voices 3..6
          const uint8_t value = (uint8_t)(3 + potZone(f, 4));
          if (value != chordVoiceCount) {
            chordVoiceCount = value;
            *putU(putStr(b, "VOICES "), chordVoiceCount) = 0;
            setOverlay(b);
          }
        } else if (i == 4) {                    // POT5 = spread
          const uint8_t value = potZone(f, 4);
          if (value != chordSpread) {
            chordSpread = value;
            *putU(putStr(b, "SPREAD "), (uint16_t)(chordSpread + 1)) = 0;
            setOverlay(b);
          }
        } else if (i == 5) {                    // POT6 = progression bank
          const uint8_t value = potZone(f, CHORD_BANK_COUNT);
          if (value != chordBank) {
            chordBank = value;
            *putU(putStr(b, "BANK "), (uint16_t)(chordBank + 1)) = 0;
            setOverlay(b);
          }
        } else if (i == 6) {                    // POT7 = chord register
          const int8_t value = (int8_t)potZone(f, 5) - 2;
          if (value != chordRegister) {
            chordRegister = value;
            previousVoiceCount = 0;
            neoState = NEO_INVALID;
            char *p = putStr(b, "REGISTER ");
            if (chordRegister >= 0) *p++ = '+';
            *putI(p, chordRegister) = 0;
            setOverlay(b);
          }
        } else if (i == 7) {                    // POT8 = auto voice leading
          const bool value = f >= 512;
          if (value != chordVoiceLeading) {
            chordVoiceLeading = value;
            setOverlay(value ? "VOICE LEAD ON" : "VOICE LEAD OFF");
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

bool oledWireWrite(uint8_t control, const uint8_t *data, uint8_t length) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(control);
  Wire.write(data, length);
  uint8_t error = Wire.endTransmission();
  const bool timedOut = Wire.getWireTimeoutFlag();
  if (timedOut) Wire.clearWireTimeoutFlag();
  if (!error && !timedOut) return true;
  hasOled = false;
  oledTxSlot = OLED_PAGE_COUNT;
  return false;
}

bool oledSendPage(uint8_t page) {
  Wire.setClock(OLED_I2C_HZ);
  const uint8_t address[] = {
    SSD1306_PAGEADDR, page, page,
    SSD1306_COLUMNADDR, 0, OLED_WIDTH_PX - 1
  };
  if (!oledWireWrite(0x00, address, sizeof(address))) return false;

  const uint8_t *src =
      display.getBuffer() + (uint16_t)page * OLED_WIDTH_PX;
  uint8_t left = OLED_WIDTH_PX;
  while (left) {
    const uint8_t n = left > OLED_DATA_PER_TX ? OLED_DATA_PER_TX : left;
    if (!oledWireWrite(0x40, src, n)) return false;
    src += n;
    left -= n;
  }
  return true;
}

void oledSendNextPage() {
  const uint8_t page = (oledTxSlot + 3) & 0x03;
  if (oledSendPage(page)) oledTxSlot++;
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
}

void applyBrightness() {
  if (!hasOled) return;
  Wire.setClock(OLED_I2C_HZ);
  const uint8_t command[] = {SSD1306_SETCONTRAST, oledBrightness};
  oledWireWrite(0x00, command, sizeof(command));
}

void displayTask() {
  const uint32_t now = millis();
  if (overlayUntil && (int32_t)(now - overlayUntil) >= 0) {
    overlayUntil = 0;
    previewNote = -1;
    dispDirty = true;
  }
  if (!hasOled) return;

  // Framebuffer не меняем в середине кадра: одна 128-byte page за loop.
  if (oledTxSlot < OLED_PAGE_COUNT) {
    oledSendNextPage();
    return;
  }
  if (!dispDirty || now - dispLastDraw < 40) return;
  dispLastDraw = now;
  dispDirty = false;
  drawScreen();
  oledTxSlot = 0;
  oledSendNextPage();             // status/overlay появляется первым
}

void ledTask() {
  if (ledOffAt && (int32_t)(millis() - ledOffAt) >= 0) {
    ledOffAt = 0;
    rgbWrite(ledBaseColor);
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
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  digitalWrite(PIN_LED_R, LOW);
  digitalWrite(PIN_LED_G, LOW);
  digitalWrite(PIN_LED_B, LOW);

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

  // Defaults применяются внутри loadSettings(); валидный блок TMD1 затем
  // заменяет их. Ни byte 0..9, ни официальные radio IDs здесь не пишутся.
  loadSettings();

#if ENABLE_NRF_RX
  setupRadioReceiver();
#endif
  updateLedBase();

  Wire.begin();
  Wire.setWireTimeout(OLED_WIRE_TIMEOUT_US, true);
  Wire.setClock(OLED_I2C_HZ);
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
    oledTxSlot = OLED_PAGE_COUNT;
  }

  // первичное чтение — без пачки событий на старте
  for (uint8_t i = 0; i < N_POTS; i++) {
    uint16_t raw = readPackedNode(POT_NODES, i);
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
  UsbMidiTx::task();
  displayTask();
  ledTask();
  settingsTask();
}
