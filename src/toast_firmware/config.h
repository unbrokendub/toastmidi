#pragma once
#include <Arduino.h>

// ============================================================
//  TOAST v1.1 — подтверждённая конфигурация конкретной платы
//
//  Аналоговая матрица была снята зондом на живом устройстве, а
//  распиновка и протокол nRF24L01+ восстановлены из оригинальной
//  прошивки v1.7 и затем подтверждены реальными RX-пакетами.
//
//  ВАЖНО: нумерация MIDI-каналов внутри кода начинается с нуля.
//  Поэтому MIDI_CH=0 означает привычный пользователю MIDI channel 1.
// ============================================================

#define MIDI_CH 0            // стартовый MIDI-канал 0..15 (0 = «канал 1»)

// Все органы этой конкретной платы сидят на 74HC4051. Упаковываем источник
// (верхние 2 бита) и канал (нижние 3 бита) в один байт вместо RAM-структур.
#define PACK_NODE(src, chan) (uint8_t)(((src) << 3) | (chan))
#define NODE_SRC(node)       (uint8_t)((node) >> 3)
#define NODE_CHAN(node)      (uint8_t)((node) & 0x07)

// ---- Распиновка платы (вывод probe от 2026-07-11) ----

#define MUX_S0 A1
#define MUX_S1 A2
#define MUX_S2 A3

// На ATmega32U4 эти три соседних вывода являются PF6/PF5/PF4.
// При USE_FAST_PORTF_MUX=1 адрес всех трёх мультиплексоров меняется одной
// записью PORTF вместо трёх сравнительно медленных digitalWrite().
// Отключите оптимизацию, если переносите прошивку на другую плату или
// меняете любой из MUX_S0..MUX_S2: останется универсальный медленный путь.
#define USE_FAST_PORTF_MUX 1

static const uint8_t MUX_Z[] = {A0, A6, A8};

static const uint8_t POT_NODES[] PROGMEM = {
  PACK_NODE(0, 0),  // POT1  верхний ряд, крайний левый (R24)
  PACK_NODE(0, 5),  // POT2  верхний ряд, у гнезда MIDI
  PACK_NODE(1, 6),  // POT3  верхний ряд, третий (R13)
  PACK_NODE(1, 5),  // POT4  верхний ряд, правый (R11)
  PACK_NODE(0, 6),  // POT5  средний ряд, левый (R25)
  PACK_NODE(0, 7),  // POT6  центр (R21)
  PACK_NODE(1, 0),  // POT7  правее центра (R12)
  PACK_NODE(1, 7),  // POT8  правый край (R10)
  PACK_NODE(1, 3),  // POT9  правый нижний (R9) — VALUE
};

static const uint8_t KEY_NODES[] PROGMEM = {
  PACK_NODE(1, 1),  // KEY1  (подтверждено повторным прогоном зонда 11.07.2026)
  PACK_NODE(1, 2),  // KEY2
  PACK_NODE(1, 4),  // KEY3
  PACK_NODE(2, 2),  // KEY4
  PACK_NODE(2, 1),  // KEY5
  PACK_NODE(2, 3),  // KEY6
  PACK_NODE(2, 7),  // KEY7
  PACK_NODE(2, 5),  // KEY8
  PACK_NODE(2, 4),  // KEY9
  PACK_NODE(2, 0),  // KEY10
  PACK_NODE(2, 6),  // KEY11
};

// ---- роли органов управления (v0.3 harmony engine) ----
// индексы = номер KEY/POT минус 1

#define BTN_SHIFT     3      // KEY4  — шифт
#define BTN_OCT_DOWN 10      // KEY11 — октава вниз (левый нижний угол)
#define BTN_OCT_UP    0      // KEY1  — октава вверх (правый нижний угол)

// KEY index -> игровая кнопка 0..7; 0xFF = служебная кнопка.
// Игровой порядок: KEY6, KEY10, KEY5, KEY9, KEY8, KEY7, KEY3, KEY2.
static const uint8_t KEY_TO_NOTE[] PROGMEM = {
  0xFF, 7, 6, 0xFF, 2, 0, 5, 4, 3, 1, 0xFF
};

// Пары вычисляются формулой в firmware: KEY6-POT5, KEY10-POT1,
// KEY5-POT6, KEY9-POT2, KEY8-POT7, KEY7-POT3, KEY3-POT8, KEY2-POT4.

#define POT_VALUE 8          // POT9 — селектор VALUE

#define PIN_LED_R 7          // три ноги штатного RGB LED (active HIGH)
#define PIN_LED_G 6
#define PIN_LED_B 5

// На этой фиксированной распиновке rgbWrite() питает включённые каналы через
// внутренние pull-up ATmega32U4: постоянная минимальная яркость без PWM/UI.

#define NRF_CSN_PIN 9        // подтверждено (ce_pin/csn_pin из прошивки оригинала)
#define NRF_CE_PIN  10       // CE=D10 (извлечён из прошивки; зонд ошибочно счёл «не подключён»)
// Цветовой порядок выведен из разводки; если ревизия платы меняет каналы,
// вместе с PIN_LED_* надо обновить direct-port mapping внутри rgbWrite().

// ---- конец блока распиновки ----

#define N_MUX  (sizeof(MUX_Z) / sizeof(MUX_Z[0]))
#define N_POTS (sizeof(POT_NODES) / sizeof(POT_NODES[0]))
#define N_KEYS (sizeof(KEY_NODES) / sizeof(KEY_NODES[0]))
#define N_NOTE_KEYS 8

// ---- OLED ----
#define OLED_HEIGHT 32       // штатный 0.91" OLED = 128x32
#define OLED_ADDR 0x3C
#define OLED_WIRE_TIMEOUT_US 3000UL
#define OLED_PAGE_COUNT (OLED_HEIGHT / 8)

// Не ставьте здесь 64 без отдельного аудита RAM: framebuffer 128x64 займёт
// 1024 байта вместо 512 и оставит ATmega32U4 опасно мало памяти под стек.
// 3 ms достаточно для одного Wire chunk при 400 kHz. Sender отдаёт одну
// 128-byte page за loop и прекращает кадр сразу после первого NACK/timeout.

// ---- nRF24L01+: штатная беспроводная панель кнопок ----
//
// Это НЕ BLE и НЕ универсальный беспроводной MIDI-канал. Панель передаёт
// собственные семибайтные события по протоколу nRF24. Основная прошивка
// только принимает эти события и уже локально превращает их в MIDI.
//
// Подтверждённая конфигурация оригинальной прошивки:
//   CE=D10, CSN=D9, SPI=D14/D15/D16; channel 120 (2520 MHz);
//   250 kbps, CRC16, auto-ack, retries 1500 us x 15;
//   RX pipe 1, 5-byte address 62 61 4C 4D 53 (ASCII "baLMS");
//   static payload 7 bytes, dynamic payloads disabled.
#define ENABLE_NRF_RX 1
#define NRF_CHANNEL 120
#define NRF_PAYLOAD_SIZE 7
#define NRF_ADDRESS_WIDTH 5
#define NRF_STUCK_HOLD_TIMEOUT_MS 30000UL
static const uint8_t NRF_RX_ADDRESS[NRF_ADDRESS_WIDTH] = {
  0x62, 0x61, 0x4C, 0x4D, 0x53
};

// Первые 6 байт payload — уникальный ID панели. Официальная прошивка
// хранит два привязанных ID в EEPROM по 0x0208 и 0x020E. Мы эти области
// ТОЛЬКО ЧИТАЕМ: это сохраняет привязку и возможность вернуться на
// официальную прошивку. Известный ID ниже — страховка именно для этого
// устройства, если служебные EEPROM-байты когда-нибудь случайно сотрутся.
#define NRF_EEPROM_KEYPAD_1 0x0208
#define NRF_EEPROM_KEYPAD_2 0x020E
#define NRF_ACCEPT_FALLBACK_ID 1
static const uint8_t NRF_FALLBACK_KEYPAD_ID[6] = {
  0x49, 0x6C, 0x4A, 0x52, 0x66, 0x34  // ASCII "IlJRf4"
};

// Сейчас физически проверена одна панель с двумя кнопками:
//   code 0: press=0x40, release=0x00
//   code 1: press=0x41, release=0x01
// В EEPROM предусмотрен ID второй такой панели, поэтому таблица ниже
// сразу имеет четыре логических места: panel1/code0, panel1/code1,
// panel2/code0, panel2/code1. Пока второго пульта нет, последние две
// строки просто никогда не вызываются.
// Если батарея пульта исчезнет именно между PRESS и RELEASE, радио уже не
// сможет сообщить отпускание. Поэтому после 30 секунд непрерывного hold
// прошивка принудительно посылает release; 0 отключил бы эту страховку.
#define NRF_BUTTONS_PER_PANEL 2
#define NRF_PANEL_COUNT 2
#define NRF_BUTTON_COUNT (NRF_BUTTONS_PER_PANEL * NRF_PANEL_COUNT)

enum RadioAction : uint8_t {
  RADIO_ACTION_DISABLED = 0,
  RADIO_ACTION_SCALE_NOTE,
  RADIO_ACTION_CC_MOMENTARY
};

struct RadioButtonDef {
  uint8_t action;
  uint8_t number;
  uint8_t pressValue;
  uint8_t releaseValue;
};

static const RadioButtonDef NRF_BUTTONS[NRF_BUTTON_COUNT] PROGMEM = {
  // Для SCALE_NOTE поле number — номер игровой ноты 0..7. Нота берётся
  // из текущего скейла точно так же, как у соответствующей кнопки корпуса.
  {RADIO_ACTION_SCALE_NOTE, 0, 127, 0},  // panel 1, code 0 (проверено)
  {RADIO_ACTION_SCALE_NOTE, 1, 127, 0},  // panel 1, code 1 (проверено)
  {RADIO_ACTION_SCALE_NOTE, 2, 127, 0},  // panel 2, code 0 (задел)
  {RADIO_ACTION_SCALE_NOTE, 3, 127, 0},  // panel 2, code 1 (задел)

  // Чтобы позднее сделать кнопку MIDI CC, замените нужную строку, например:
  // {RADIO_ACTION_CC_MOMENTARY, 64, 127, 0}
  // Здесь 64 — номер CC, 127 отправится при нажатии, 0 — при отпускании.
  // RADIO_ACTION_DISABLED полностью отключает соответствующее место.
};

// ---- поведение ----
#define POT_SEND_THRESHOLD 6   // защита от дребезга АЦП (в отсчётах 0..1023)
#define POT_ARM_THRESHOLD 16   // насколько сдвинуть пот, чтобы он «проснулся»
                               // после смены режима (защита от скачков)
#define KEY_DEBOUNCE_MS 15
#define KEY_VELOCITY 127
#define OVERLAY_MS 1400        // сколько держать подсказку на экране

// ---- фиксированная яркость OLED (контраст SSD1306) ----
// Минимум держим выше нуля, чтобы экран не гас полностью.
#define OLED_BRIGHTNESS_MIN 6

// ---- harmony engine ----
#define CHORD_VOICES 6

// ---- сохранение пользовательских настроек ----
// По умолчанию runtime-настройки НЕ пишутся: прибор быстро настраивается,
// EEPROM не изнашивается, а код writer/CRC не занимает flash. Официальные
// radio IDs по 0x0208..0x0213 по-прежнему только читаются. Writer v3
// сохранён для экспериментов, но ENABLE=1 требует нового size-аудита.
#define ENABLE_SETTINGS_PERSISTENCE 0
#define SETTINGS_EEPROM_START 10
#define SETTINGS_SAVE_DELAY_MS 10000UL
