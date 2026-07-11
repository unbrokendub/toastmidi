/*
 * TOAST — кастомная прошивка v0.1 «скейл-клавиатура»
 * SpaceMelodyLab TOAST v1.1 (Arduino Pro Micro / ATmega32U4)
 *
 * Управление:
 *   8 игровых кнопок       — ноты (полифония, аккорды работают)
 *   KEY11 / KEY1           — октава вниз / вверх
 *   KEY4 (SHIFT) + POT9    — выбор скейла
 *   KEY4 + POT1..8         — нота закреплённой кнопки (внутри скейла)
 *   KEY4 + KEY11 + POT9    — MIDI-канал
 *   KEY4 + KEY11 + POT1..8 — CC-номер пота
 *   KEY4 + KEY1  + POT9    — тоника (root) скейла
 *   POT1..8 без шифта      — MIDI CC (по умолчанию CC1..CC8)
 *
 * Все события идут в USB-MIDI и в DIN/TRS MIDI OUT одновременно.
 * OLED: клавиатура одной октавы + статус + подсказки действий.
 */

#include <Arduino.h>
#include <Wire.h>
#include <MIDIUSB.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

#if ENABLE_NRF
#include <SPI.h>
#include <RF24.h>
RF24 radio(NRF_CE_PIN, NRF_CSN_PIN);
bool nrfOk = false;
#endif

Adafruit_SSD1306 display(128, OLED_HEIGHT, &Wire, -1);
bool hasOled = false;

// ================== скейлы ==================

struct ScaleDef {
  uint8_t len;
  uint8_t deg[12];
  char name[11];
};

const ScaleDef SCALES[] PROGMEM = {
  {12, {0,1,2,3,4,5,6,7,8,9,10,11}, "CHROMATIC"},
  {7,  {0,2,4,5,7,9,11},            "MAJOR"},
  {7,  {0,2,3,5,7,8,10},            "MINOR"},
  {7,  {0,2,3,5,7,8,11},            "HARM MINOR"},
  {7,  {0,2,3,5,7,9,11},            "MEL MINOR"},
  {7,  {0,2,3,5,7,9,10},            "DORIAN"},
  {7,  {0,1,3,5,7,8,10},            "PHRYGIAN"},
  {7,  {0,2,4,6,7,9,11},            "LYDIAN"},
  {7,  {0,2,4,5,7,9,10},            "MIXOLYDIAN"},
  {7,  {0,1,3,5,6,8,10},            "LOCRIAN"},
  {5,  {0,2,4,7,9},                 "PENTA MAJ"},
  {5,  {0,3,5,7,10},                "PENTA MIN"},
  {6,  {0,3,5,6,7,10},              "BLUES MIN"},
  {6,  {0,2,3,4,7,9},               "BLUES MAJ"},
  {6,  {0,2,4,6,8,10},              "WHOLE TONE"},
  {8,  {0,2,3,5,6,8,9,11},          "DIM W-H"},
  {8,  {0,1,3,4,6,7,9,10},          "DIM H-W"},
  {7,  {0,1,4,5,7,8,10},            "PHRYG DOM"},
  {7,  {0,2,3,6,7,8,11},            "HUNGAR MIN"},
  {7,  {0,1,4,5,7,8,11},            "DBL HARMON"},
  {5,  {0,2,3,7,8},                 "HIRAJOSHI"},
  {5,  {0,1,5,7,10},                "IN SEN"},
  {5,  {0,1,5,6,10},                "IWATO"},
  {5,  {0,2,5,7,9},                 "YO"},
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
int16_t  ladderMax = 127;       // последний индекс «лестницы» скейла (нота <=127)
int16_t  keyLadder[8];          // позиция каждой игровой кнопки в лестнице
uint8_t  potCCnum[9];           // CC-номер каждого пота
int8_t   potToKey[9];           // обратная карта: пот -> игровая кнопка (-1 = value)

Mode     mode = M_PLAY;
bool     swapLayers = false;    // true: поты без шифта крутят ноты, CC — под шифтом
bool     comboLatched = false;  // защёлка комбо SHIFT+обе октавы
int8_t   previewNote = -1;      // нота, выбираемая потом — подсветка на пиано
uint8_t  velocity = KEY_VELOCITY;  // velocity нот, крутится value-потом в игре

uint16_t potFilt[9];
uint16_t potLatch[9];
bool     potArmed[9];
uint8_t  potSent7[9];

bool     keyState[N_KEYS];      // подтверждённые состояния всех 11 кнопок
bool     keyRead[N_KEYS];
uint32_t keyT[N_KEYS];
uint8_t  sounding[8];           // звучащая нота игровой кнопки, 0xFF = тишина

bool     usbDirty  = false;
bool     dispDirty = true;
uint32_t ledOffAt = 0, dispLastDraw = 0, overlayUntil = 0;
char     overlay[22] = "";
char     lastEvent[22] = "TOAST v0.1";

// ================== утилиты ==================

void fmtNote(char *out, uint8_t n) {          // "C#3" (C3 = MIDI 60)
  snprintf(out, 5, "%s%d", NOTE_NAMES[n % 12], n / 12 - 2);
}

// k-я ступень лестницы скейла (все ноты скейла подряд от MIDI 0)
int16_t ladderNote(int16_t k) {
  return rootPC + 12 * (k / curScale.len) + curScale.deg[k % curScale.len];
}

uint8_t playedNote(uint8_t j) {               // с учётом сдвига октав
  int16_t n = ladderNote(keyLadder[j]) + 12 * octOffset;
  while (n > 127) n -= 12;
  while (n < 0) n += 12;
  return (uint8_t)n;
}

void resetLadder() {                          // кнопки = первые 8 ступеней от C3
  for (uint8_t j = 0; j < 8; j++) keyLadder[j] = 5 * curScale.len + j;
}

void setScale(uint8_t s) {
  scaleIdx = s;
  memcpy_P(&curScale, &SCALES[s], sizeof(ScaleDef));
  ladderMax = 0;
  while (ladderNote(ladderMax + 1) <= 127) ladderMax++;
  resetLadder();
}

void setOverlay(const char *s) {
  strncpy(overlay, s, sizeof(overlay) - 1);
  overlay[sizeof(overlay) - 1] = 0;
  overlayUntil = millis() + OVERLAY_MS;
  previewNote = -1;             // каждая новая подсказка сама решает, что подсвечивать
  dispDirty = true;
}

// ================== чтение входов ==================

uint16_t readNode(uint8_t src, uint8_t chan) {
  uint8_t pin;
  if (src == 0xFF || src == 0xFE) {
    pin = chan;
  } else {
    digitalWrite(MUX_S0, chan & 1);
    digitalWrite(MUX_S1, (chan >> 1) & 1);
    digitalWrite(MUX_S2, (chan >> 2) & 1);
    delayMicroseconds(50);
    pin = MUX_Z[src];
  }
  analogRead(pin);
  return analogRead(pin);
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

#if ENABLE_NRF
void nrfSend3(uint8_t a, uint8_t b, uint8_t c) {
  if (!nrfOk) return;
  uint8_t buf[3] = {a, b, c};
  radio.write(buf, 3, true);
}
#else
#define nrfSend3(a, b, c)
#endif

void sendCC(uint8_t cc, uint8_t val) {
  midiEventPacket_t p = {0x0B, (uint8_t)(0xB0 | midiCh), cc, val};
  MidiUSB.sendMIDI(p);
  usbDirty = true;
  dinWrite3(0xB0 | midiCh, cc, val);
  nrfSend3(0xB0 | midiCh, cc, val);
  ledFlash();
  snprintf(lastEvent, sizeof(lastEvent), "CC%u = %u", cc, val);
  dispDirty = true;
}

void sendNote(uint8_t note, bool on) {
  uint8_t st = (on ? 0x90 : 0x80) | midiCh;
  uint8_t vel = on ? velocity : 0;
  midiEventPacket_t p = {(uint8_t)(on ? 0x09 : 0x08), st, note, vel};
  MidiUSB.sendMIDI(p);
  usbDirty = true;
  dinWrite3(st, note, vel);
  nrfSend3(st, note, vel);
  ledFlash();
  if (on) {
    char nm[5];
    fmtNote(nm, note);
    snprintf(lastEvent, sizeof(lastEvent), "%s", nm);
  }
  dispDirty = true;
}

// ================== кнопки ==================

int8_t noteKeyIndexOf(uint8_t keyIdx) {
  for (uint8_t j = 0; j < 8; j++)
    if (NOTE_KEYS[j] == keyIdx) return j;
  return -1;
}

void onKeyChange(uint8_t idx, bool down) {
  if (idx == BTN_SHIFT) { dispDirty = true; return; }

  if (idx == BTN_OCT_DOWN || idx == BTN_OCT_UP) {
    if (down && !keyState[BTN_SHIFT]) {          // без шифта — октава
      int8_t d = (idx == BTN_OCT_UP) ? 1 : -1;
      int8_t no = octOffset + d;
      if (no >= -3 && no <= 3) octOffset = no;
      char b[16];
      snprintf(b, sizeof(b), "OCTAVE %+d", octOffset);
      setOverlay(b);
    }
    return;
  }

  int8_t j = noteKeyIndexOf(idx);
  if (j < 0) return;

  if (down) {
    if (mode == M_PLAY && sounding[j] == 0xFF) {
      sounding[j] = playedNote(j);
      sendNote(sounding[j], true);
    } else if (mode == M_SHIFT && j == 0) {      // SHIFT + первая кнопка
      resetLadder();
      setOverlay("NOTES RESET");
    }
  } else {
    if (sounding[j] != 0xFF) {                   // note-off отдаём всегда
      sendNote(sounding[j], false);
      sounding[j] = 0xFF;
    }
  }
}

void updateKeys() {
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
      onKeyChange(i, keyState[i]);
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

void updateMode() {
  // SHIFT + обе октавные кнопки = поменять слои потов (CC <-> ноты)
  bool combo = keyState[BTN_SHIFT] && keyState[BTN_OCT_DOWN] &&
               keyState[BTN_OCT_UP];
  if (combo && !comboLatched) {
    comboLatched = true;
    swapLayers = !swapLayers;
    latchPots();
    setOverlay(swapLayers ? "POTS = NOTES" : "POTS = CC");
  } else if (!combo) {
    comboLatched = false;
  }

  Mode m = M_PLAY;
  if (keyState[BTN_SHIFT]) {
    m = M_SHIFT;
    if (keyState[BTN_OCT_DOWN]) m = M_SETUP;
    else if (keyState[BTN_OCT_UP]) m = M_ROOT;
  }
  if (m != mode) {
    mode = m;
    latchPots();
    dispDirty = true;
  }
}

void potSendCC(uint8_t i, uint16_t f) {
  uint8_t v7 = f >> 3;
  if (v7 != potSent7[i]) {
    potSent7[i] = v7;
    sendCC(potCCnum[i], v7);
  }
}

void assignKeyNote(uint8_t j, uint16_t filt) {
  int16_t k = (int32_t)filt * (ladderMax + 1) >> 10;
  if (k > ladderMax) k = ladderMax;
  if (k == keyLadder[j]) return;
  keyLadder[j] = k;
  char b[20], nm[5];
  uint8_t pn = playedNote(j);
  fmtNote(nm, pn);
  snprintf(b, sizeof(b), "BTN%u = %s", j + 1, nm);
  setOverlay(b);
  previewNote = pn;             // показать выбираемую ноту и на клавиатуре
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
          uint8_t v = f >> 3;
          if (v < 1) v = 1;                      // 0 = note-off, нельзя
          if (v != velocity) {
            velocity = v;
            snprintf(b, sizeof(b), "VELOCITY %u", velocity);
            setOverlay(b);
          }
          break;
        }
        if (swapLayers) {
          if (potToKey[i] >= 0) assignKeyNote(potToKey[i], f);
        } else {
          potSendCC(i, f);
        }
        break;

      case M_SHIFT:
        if (i == POT_VALUE) {
          uint8_t s = (uint32_t)f * N_SCALES >> 10;
          if (s != scaleIdx) {
            setScale(s);
            snprintf(b, sizeof(b), "%s", curScale.name);
            setOverlay(b);
          }
        } else if (swapLayers) {
          potSendCC(i, f);
        } else if (potToKey[i] >= 0) {
          assignKeyNote(potToKey[i], f);
        }
        break;

      case M_SETUP:
        if (i == POT_VALUE) {
          uint8_t c = f >> 6;
          if (c != midiCh) {
            midiCh = c;
            snprintf(b, sizeof(b), "CHANNEL %u", midiCh + 1);
            setOverlay(b);
          }
        } else {
          uint8_t cc = f >> 3;
          if (cc != potCCnum[i]) {
            potCCnum[i] = cc;
            snprintf(b, sizeof(b), "POT%u > CC%u", i + 1, cc);
            setOverlay(b);
          }
        }
        break;

      case M_ROOT:
        if (i == POT_VALUE) {
          uint8_t r = (uint32_t)f * 12 >> 10;
          if (r != rootPC) {
            rootPC = r;
            setScale(scaleIdx);                  // пересчёт лестницы
            snprintf(b, sizeof(b), "ROOT %s", NOTE_NAMES[rootPC]);
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
    uint8_t x = PC_GAP[pc] * 9 + 6;
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
  for (uint8_t j = 0; j < 8; j++)
    if (sounding[j] != 0xFF) pcMask |= 1u << (sounding[j] % 12);
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
}

void displayTask() {
  if (overlayUntil && millis() > overlayUntil) {
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
  if (PIN_LED >= 0 && ledOffAt && millis() >= ledOffAt) {
    digitalWrite(PIN_LED, LOW);
    ledOffAt = 0;
  }
}

// ================== инициализация ==================

void setup() {
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  for (uint8_t z = 0; z < N_MUX; z++) pinMode(MUX_Z[z], INPUT);
  for (uint8_t i = 0; i < N_KEYS; i++)
    if (KEYS[i].src == 0xFE)
      pinMode(KEYS[i].chan, KEYS[i].activeLow ? INPUT_PULLUP : INPUT);
  if (PIN_LED >= 0) {
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
  }

  Serial1.begin(31250);                        // DIN/TRS MIDI OUT

  setScale(0);                                 // хроматика от C3
  for (uint8_t j = 0; j < 8; j++) sounding[j] = 0xFF;
  for (uint8_t i = 0; i < N_POTS; i++) {
    potCCnum[i] = POTS[i].cc;
    potToKey[i] = -1;
  }
  for (uint8_t j = 0; j < 8; j++) potToKey[KEY_POT[j]] = j;

  Wire.begin();
  hasOled = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (hasOled) {
    Wire.setClock(400000UL);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(3);
    display.setCursor(10, 5);
    display.print(F("TOAST"));
    display.display();
    delay(800);
  }

#if ENABLE_NRF
  nrfOk = radio.begin();
  if (nrfOk) {
    radio.setChannel(NRF_CHANNEL);
    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_HIGH);
    radio.setAutoAck(false);
    radio.openWritingPipe((const uint8_t *)NRF_ADDR);
    radio.stopListening();
  }
#endif

  // первичное чтение — без пачки событий на старте
  for (uint8_t i = 0; i < N_POTS; i++) {
    uint16_t raw = readNode(POTS[i].src, POTS[i].chan);
    potFilt[i] = raw;
    potSent7[i] = raw >> 3;
  }
  latchPots();
  for (uint8_t i = 0; i < N_POTS; i++) potArmed[i] = true;  // CC-поты живые сразу
  potArmed[POT_VALUE] = false;  // velocity = 127, пока value-пот не тронут
  for (uint8_t i = 0; i < N_KEYS; i++) {
    keyState[i] = keyRead[i] = false;
    keyT[i] = 0;
  }
}

void loop() {
  updateKeys();
  updateMode();
  updatePots();
  if (usbDirty) {
    MidiUSB.flush();
    usbDirty = false;
  }
  displayTask();
  ledTask();
}
