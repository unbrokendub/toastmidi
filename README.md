# TOAST v0.3 — scale keyboard + harmony engine

Кастомная прошивка для **SpaceMelodyLab TOAST v1.1** на ATmega32U4.
Восемь кнопок работают как полифоническая скейл-клавиатура, а октавные
модификаторы открывают scale-аккорды, progression banks и P/R/L-гармонию.
Все MIDI-события одновременно идут в DIN/TRS и USB-MIDI.

## Главное

- 84 скейла, включая два неоктавных Bohlen–Pierce с периодом 19 полутонов;
- индивидуальная высота каждой из 8 кнопок через парный потенциометр;
- раскладка переживает смену скейла без потери накрученных регистров;
- 6 scale-профилей, 8 банков прогрессий, две P/R/L-палитры и 4 VL personality;
- 3–6 голосов, inversion, Close/Wide/Drop2/Drop3 и register ±2;
- MIDI channel 1–16, velocity и переназначаемые CC1–CC127;
- штатная радиопанель через запаянный nRF24L01+;
- OLED 128×32 и штатный RGB LED с постоянной минимальной яркостью;
- USB Serial/CDC оставлен включённым, поэтому загрузка не требует доступа к
  физической кнопке reset в нормальном режиме.

## Органы управления

Игровые кнопки слева направо: KEY6 → KEY10 → KEY5 → KEY9 → KEY8 →
KEY7 → KEY3 → KEY2. Служебные кнопки: SHIFT=KEY4, OCT−=KEY11,
OCT+=KEY1.

В таблицах ниже `PAD1…PAD8` — именно этот порядок **слева направо**, а не
номер `KEY` на шелкографии. Например, физический KEY8 — это PAD5, а настоящий
PAD8/HOME — физический KEY2.

Пары кнопка–пот: KEY6–POT5, KEY10–POT1, KEY5–POT6, KEY9–POT2,
KEY8–POT7, KEY7–POT3, KEY3–POT8, KEY2–POT4. POT9 — VALUE.

### PLAY

| Действие | Результат |
|---|---|
| PAD | одиночная нота |
| короткий OCT− / OCT+ | глобальный period shift −1 / +1, диапазон ±3 |
| держать OCT− + PAD | выбранный scale-profile |
| держать OCT+ + PAD | соответствующий аккорд progression bank |
| обе OCT + PAD | P/R/L-преобразование |
| POT1…8 | назначение высоты парной кнопки, слой по умолчанию |
| POT9 | velocity 1…127 |
| SHIFT + POT1…8 | MIDI CC, слой по умолчанию |
| SHIFT + POT9 | выбор одного из 84 скейлов |
| SHIFT + обе OCT | поменять местами NOTE/CC-слои до перезагрузки |

Октавные кнопки срабатывают на отпускание. Как только обе подтверждены
одновременно или использованы с PAD, обе помечаются как модификаторы и уже
не могут случайно изменить регистр.

### Быстрая шпаргалка по аккордам

Служебные кнопки на корпусе: OCT− — физический KEY11, OCT+ — KEY1,
SHIFT — KEY4. Номер PAD ниже означает игровой порядок слева направо,
а физический KEY для каждого PAD указан в начале README.

| Что сделать | Результат |
|---|---|
| держать OCT− (KEY11) и нажать любой PAD | scale-profile chord от ноты этого PAD |
| держать OCT+ (KEY1) и нажать PAD1…PAD8 | шаг 1…8 выбранного progression bank |
| держать обе OCT и нажать PAD | операция P/R/L из активной палитры BASIC или EXT |
| SHIFT + OCT− (KEY11), затем двигать POT1…POT9 | настройка root, profile, inversion, voices, spread, bank, register, VL и P/R/L palette |
| SHIFT + OCT+ (KEY1), затем двигать POT | только MIDI channel и CC-номера; аккорды здесь не настраиваются |

Пока аккордовый PAD удерживается, OCT-кнопку можно отпустить и играть
остальными PAD обычную мелодию поверх аккорда. Если нажать следующий аккорд
до отпускания предыдущего, общие ноты не перезапускаются.

Быстрый выбор profile без потенциометров:

| Комбинация | Физический PAD-key | Profile |
|---|---|---|
| SHIFT + PAD2 | KEY10 | Wide9 |
| SHIFT + PAD3 | KEY5 | Open |
| SHIFT + PAD4 | KEY9 | 13no11 |
| SHIFT + PAD5 | KEY8 | Shell9 |
| SHIFT + PAD6 | KEY7 | Quartal |
| SHIFT + PAD7 | KEY3 | Quintal |

`SHIFT + PAD8` — физический KEY2 — переключает P/R/L BASIC/EXT. При этой
команде обе OCT должны быть отпущены.

### SETUP: SHIFT + OCT+

| Пот | Параметр |
|---|---|
| POT1…8 | CC-номер соответствующего потенциометра |
| POT9 | MIDI channel 1…16 |

### ROOT/HARMONY: SHIFT + OCT−

| Пот | Параметр |
|---|---|
| POT1 | root C…B |
| POT2 | scale-profile |
| POT3 | inversion 0…5 |
| POT4 | 3…6 голосов |
| POT5 | Close / Wide / Drop2 / Drop3 |
| POT6 | progression bank |
| POT7 | chord register −2…+2 |
| POT8 | VL: 0 OFF / 1 SMOOTH / 2 BASS / 3 TOP |
| POT9 | палитра P/R/L: BASIC / EXT |

После входа в новый режим каждый пот ждёт реального движения: старое
физическое положение не вызывает скачок параметра.
POT9 и быстрая команда SHIFT + PAD8 управляют одной и той же P/R/L-палитрой.

### Быстрые SHIFT-команды

| Комбинация | Результат |
|---|---|
| SHIFT + PAD1 | NOTES RESET |
| SHIFT + PAD2…PAD7 | прямой выбор шести profiles |
| SHIFT + PAD8 без OCT | палитра P/R/L: BASIC ↔ EXT |

## Относительная раскладка

Для каждой кнопки хранятся две величины:

- `keyRelSemitone[]` — каноническая желаемая высота относительно root;
- `keyLadder[]` — производный индекс ближайшей ступени текущего скейла.

При смене скейла меняется только второй массив. Например, кнопки,
индивидуально поднятые на октаву в Minor, останутся в том же регистре после
перехода в Major и точно восстановятся при возврате в Minor. В редком скейле
две кнопки могут временно попасть на одну ближайшую ноту — исходные anchors
при этом не теряются. Явное движение парного потенциометра записывает новое
намерение пользователя.

Смена root транспонирует anchors вместе с тоникой. Глобальные OCT-кнопки
используют `curScale.period`: 12 для обычных скейлов и 19 для Bohlen–Pierce.

Последние восемь позиций селектора — новые скейлы: BEBOP DOR, BLUES BOTH,
MIN6 PENTA, LYD AUG, LYD b3, LEAD WHOLE, HMIN PENTA и TODI 12T. Последний —
12-TET-аппроксимация для экспериментов с drone, а не полная ладовая модель
индийской раги.

## Harmony engine

При нажатии аккорда нижняя строка OLED показывает его имя. Для progression
и P/R/L это обычные символы вроде `Cmaj`, `Cm`, `G7`, `Cmaj7`, `Dm7`; для
scale-profile — root и короткое имя профиля, например `C W9`, `D OPEN`,
`F 13N11`, `G SH9`, `A 4TH`, `B 5TH`. Inversion, spread, число голосов и VL
меняют voicing, но не гармоническое имя. После Note Off название остаётся
последним событием; следующая нота, CC или новый аккорд его заменяет. Имена
нот используют диезы. Для Bohlen–Pierce это условное 12-TET-имя смыслового
root до inversion/spread/VL, а не буквальное имя баса и не полноценная
BP-нотация.

### Scale-profiles — OCT− + PAD

Профили заданы смещениями в ступенях текущего скейла и поэтому всегда
остаются внутри него:

1. Wide9
2. Open
3. 13no11
4. Shell9
5. Quartal
6. Quintal

### Progression banks — OCT+ + PAD1…8

| Bank | Последовательность восьми PAD |
|---|---|
| Pop | I – V – vi – IV, повтор |
| Jazz | ii7 – V7 – Imaj7 – vi7, повтор |
| Borrow | I – IV – iv – I, повтор |
| Mixolyd | ♭VII – IV – I – ♭VII – IV – I – ♭VII – I |
| Aeolian | i – ♭VII – ♭VI – ♭VII, повтор |
| Heroic | ♭VI – ♭VII – I – ♭VI – ♭VII – I – ♭VII – I |
| Dorian | i – IV, четыре раза |
| Mediants | I – III – ♭VI – I, повтор |

Scale-profiles следуют периоду выбранного скейла. Progression banks и P/R/L
намеренно строят привычные интервалы и chord register в 12-TET, даже если
текущий скейл — Bohlen–Pierce. При этом глобальный OCT shift везде следует
`curScale.period`, то есть сдвигает BP-аккорды на 19 полутонов.

### P/R/L — обе OCT + PAD

| PAD слева направо | Физический KEY | BASIC | EXT |
|---|---|---|---|
| 1 | KEY6 | P (Parallel) | S (Slide) |
| 2 | KEY10 | R (Relative) | N (Nebenverwandt) |
| 3 | KEY5 | L (Leading-tone exchange) | H (Hexatonic Pole) |
| 4 | KEY9 | P → R | P → R |
| 5 | KEY8 | P → L | P → L |
| 6 | KEY7 | R → P | NEG (root axis) |
| 7 | KEY3 | R → L | R → L |
| 8 | KEY2 | HOME | HOME |

После включения активна BASIC. `SHIFT + PAD8` без OCT переключает палитру;
это крайняя правая игровая кнопка, физический KEY2, а не кнопка с надписью
KEY8. Переключение меняет только следующие операции и сохраняет текущую
гармоническую точку. Составные операции выполняются слева направо.

Примеры от C major: P → C minor, R → A minor, L → E minor. Последний
progression chord становится seed для следующей P/R/L-операции. P/R/L state
хранит pitch class, а не абсолютную MIDI-октаву; HOME дополнительно сбрасывает
VL history и потому при повторных нажатиях всегда даёт один и тот же voicing.
В EXT от C major: S → C# minor, N → F minor, H → Ab minor. NEG отражает
текущую major/minor-триаду относительно оси выбранного root и меняет её лад.

Auto voice leading перебирает обращения и соседние регистры, минимизируя
движение голосов относительно последнего сыгранного аккорда. SMOOTH считает
все голоса одинаково, BASS в четыре раза сильнее удерживает нижний голос,
TOP — верхний. Новая personality применяется к следующему аккорду и не
перестраивает уже удерживаемый.
История не зависит от held-note bookkeeping и сохраняется после отпускания.
Изменение числа голосов начинает новую историю автоматически. При любом VL
кроме OFF POT3 задаёт предпочтение при равном результате, а не принудительное
обращение; строгий inversion получается при VL OFF. Shell9 по определению
остаётся четырёхголосным, даже если общий параметр VOICES равен 5 или 6.
Voice leading рассматривает только обращения в окне вокруг заданного register,
поэтому циклы P/R/L больше не могут постепенно убежать к MIDI 0 или 127.

На границах MIDI переносится **весь voicing** на целый period. Отдельные
голоса больше не отбрасываются. Перед отправкой точные дубли удаляются, а
Note Off всегда использует фактические ноты и канал, сохранённые при press.

## MIDI transport и отзывчивость

- Каждая пачка сначала целиком отправляется в DIN, затем попадает в USB queue.
- USB-MIDI пишет только при `USB_SendSpace(5) >= 8`; 250-ms blocking path и
  ожидание ZLP при заполнении 64-byte bank недостижимы.
- Очередь содержит 16 USB MIDI packets. Непрерывные pot CC coalesce.
- При переполнении USB запускает All Sound Off (CC120) на всех 16 каналах;
  DIN продолжает работать независимо. Уже удерживаемые в момент recovery
  USB-ноты надо перепрессовать.
- OLED использует framebuffer Adafruit GFX, но выгружает одну 128-byte page
  за loop в порядке 3,0,1,2. Нижний status/overlay появляется первым, а одна
  итерация блокируется I²C примерно на 3,2 мс вместо полного кадра ~12 мс.
- При I²C NACK/timeout OLED отключается после первой ошибочной транзакции;
  MIDI-loop продолжает работать.

RGB работает с постоянной минимальной яркостью через слабые внутренние
pull-up, без runtime-регулировки и PWM. В обычной игре он красный;
`SHIFT + OCT−` (настройки аккордов) включает зелёный, чистый SHIFT — жёлтый,
а `SHIFT + OCT+` (MIDI/CC setup) — magenta. MIDI event даёт тусклую короткую
белую вспышку и возвращает цвет режима.

## EEPROM

`ENABLE_SETTINGS_PERSISTENCE` по умолчанию равен `0`: текущий скейл,
раскладка, CC и остальные runtime-настройки **не записываются**. При каждом
старте прибор получает defaults и быстро настраивается с панели. Старый
custom-блок EEPROM не стирается.

Штатные IDs радиопанелей в `0x0208..0x0213` только читаются; pairing и
возможность возврата к официальной прошивке не затрагиваются. В исходнике
оставлен writer формата v3 за compile-time flag, но feature-full сборка при
его включении требует отдельного size-аудита. ATmega32U4 гарантирует не менее
100 000 циклов EEPROM erase/write на ячейку; при штатной конфигурации v0.3
число записей равно нулю.

## Радиопанель

Запаянный модуль — nRF24L01+, не BLE. Подтверждённые параметры оригинала:
CE=D10, CSN=D9, SPI=D14/D15/D16, channel 120 (2520 MHz), 250 kbit/s,
CRC16, auto-ack, pipe 1 `baLMS`, payload 7 bytes.

Первые две подтверждённые radio-кнопки дублируют PAD1/PAD2. Таблица
`NRF_BUTTONS[]` в `src/toast_firmware/config.h` позволяет назначить scale
note, momentary CC или disabled. Если пульт потеряет питание между press и
release, 30-секундный timeout сам отпустит ноту/CC.

## Сборка

```bash
pio run -e toast_firmware
pio run -e toast_firmware -t upload
```

Проверенная конфигурация: SparkFun Pro Micro 5V/16 MHz, PlatformIO
`atmelavr@5.3.0`. USB CDC специально не отключён. Размер v0.3 на текущем
toolchain: 28 366 / 28 672 bytes flash (98,9%), 1 374 / 2 560 bytes static
RAM; SSD1306 framebuffer ещё 512 bytes выделяет в heap при старте.

Size flags находятся в `platformio.ini`: splash SSD1306 отключён,
`-mcall-prologues`, AVR linker relaxation и уменьшенный неиспользуемый RX
buffer Serial1. TX buffer остаётся 64 bytes, чтобы DIN-аккорд не блокировался.

Arduino IDE можно использовать через
`src/toast_firmware/toast_firmware.ino`, если глобально определить
`SSD1306_NO_SPLASH` и установить MIDIUSB, Adafruit SSD1306 и Adafruit GFX.

## Файлы

- `src/toast_firmware/firmware_main.cpp` — runtime, music engine и I/O;
- `src/toast_firmware/config.h` — распиновка, radio map и compile-time flags;
- `platformio.ini` — воспроизводимая сборка;
- `src/toast_firmware/toast_firmware.ino` — Arduino IDE wrapper.
