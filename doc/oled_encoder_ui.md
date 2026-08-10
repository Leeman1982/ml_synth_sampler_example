# On-device UI: SH1106 OLED + rotary encoder + buttons (RP2040 / RP2350)

This adds a full on-device menu to the sampler for Raspberry Pi Pico
(RP2040) and Pico 2 (RP2350): a 128x64 SH1106 I2C OLED, a rotary encoder
with push-select, and two momentary buttons (Back / Home). Every parameter
that was previously only reachable via a MIDI CC (see
[`z_config.ino`](../z_config.ino)) is now also reachable from this menu -
sample loading, sampler parameters, all wired-up effects, and input gain.

The implementation lives in [`ui.h`](../ui.h) / [`ui.cpp`](../ui.cpp) and is
gated entirely by `UI_OLED_ENABLED`, defined in
[`config/config_rp2040.h`](../config/config_rp2040.h) and
[`config/config_rp2350.h`](../config/config_rp2350.h). It compiles to a no-op
on every other board.

## Boot splash and branding

On boot the display shows a **O.C.P / DELTA CITY / MK1 / by ZOMBI SS**
splash screen for ~2.2 seconds (any button/encoder input skips it
immediately), then drops into the Home screen. Every screen title/heading
throughout the menu uses the same stylized bitmap font. See
[`doc/branding.md`](branding.md) for font attribution/licensing and how to
regenerate/extend it.

## Hardware

- **Display:** SH1106 1.3" 128x64 I2C OLED (NOT SSD1306 - the driver chip
  matters). Often sold on a combo module with a 4x4 keypad; only the OLED's
  four pins are used here (`SDA`, `SCL`, `VCC`, `GND`).
- **Rotary encoder** with push-button (e.g. an EC11), switch-to-ground style.
- **2x momentary push buttons**, switch-to-ground style (Back, Home).

## Wiring

```
Component          Signal      RP2040 / RP2350 GPIO
──────────────      ──────      ─────────────────────
SH1106 OLED         SDA         GP16  (I2C0 SDA)
SH1106 OLED         SCL         GP17  (I2C0 SCL)
SH1106 OLED         VCC         3V3
SH1106 OLED         GND         GND

Rotary encoder       A          GP10
Rotary encoder       B          GP11
Rotary encoder       PUSH       GP14
Rotary encoder       GND        GND

Button "Back"        signal     GP15
Button "Back"        GND        GND
Button "Home"        signal     GP20
Button "Home"        GND        GND
```

I2C address: **0x3C** (fixed on most SH1106 modules, no jumpers). Bus speed:
400kHz. All encoder/button inputs use the internal pull-ups
(`INPUT_PULLUP`) - wire the other leg of every switch straight to GND, no
external resistors needed.

These pins were chosen to avoid every pin already used by the sampler
itself on both boards (PWM/I2S audio, MIDI, the status LED) - see
[`board_info.md`](board_info.md) and `config/config_rp2040.h` /
`config/config_rp2350.h` for what else is wired up. If you need different
pins, override `UI_OLED_I2C_SDA_PIN`, `UI_OLED_I2C_SCL_PIN`, `UI_ENC_A_PIN`,
`UI_ENC_B_PIN`, `UI_ENC_SW_PIN`, `UI_BTN_BACK_PIN`, `UI_BTN_HOME_PIN` in the
relevant config file. Any I2C0-capable pair works for the OLED (GP0/GP1,
GP4/GP5, GP8/GP9, GP12/GP13, GP16/GP17, GP20/GP21).

## Arduino IDE library requirement

Install **U8g2** by oliver (Oliver Kraus) via `Sketch` -> `Include Library`
-> `Manage Libraries...`. Do not use Adafruit_SSD1306 - it does not work
with the SH1106 driver.

## Navigation

- **Home screen:** shows sample count and sampler memory usage. Press the
  encoder to open the menu.
- **Encoder rotate:** move the selection up/down in a list, or adjust a
  value while editing.
- **Encoder push (select):** enter a submenu / start editing a value / run
  an action / confirm a value and return.
- **Back button:** go up one menu level (or back to Home from the root
  menu); cancels out of value-editing without special handling since the
  value is already applied live as you turn the encoder.
- **Home button:** jump straight back to the Home screen from anywhere.

## Menu structure

```
Menu
├── Load Sample
│   ├── Clear All Samples
│   ├── Load WAV->AllKeys      (browses *.wav on LittleFS)
│   ├── Load SF2 Samples       (browses *.sf2 on LittleFS)
│   ├── Load SF2 Instr.        (browses *.sf2 on LittleFS)
│   └── Load Full SF2          (browses *.sf2 on LittleFS)
├── Sampler
│   ├── Prev / Next Preset
│   ├── Tune Coarse
│   ├── Hold / Release / Release (per-sample)
│   ├── Loop Entire Sample
│   ├── All Notes Off
│   └── Test Note A4 (toggle)
├── Effects                     (see note below - board dependent)
│   ├── Delay Level / Feedback / Length
│   └── (RP2350 only) LFO Speed, Phaser Depth/G, Vibrato Depth/Intensity,
│       Pitch Speed/Mix/Feedback, Tremolo Depth, Reverb Level
├── Input Gain
└── System
    ├── Save Settings
    ├── Pin Info
    └── Memory Info
```

The "Load Sample" file browsers list actual files found on LittleFS at run
time (using the same helpers `loaddata_examples.ino` is built on) - copy
your `.wav`/`.sf2` files to the board's LittleFS filesystem (see
[`doc/rp2040_arduino_ide_setup.md`](rp2040_arduino_ide_setup.md) for how to
upload it) and they will show up automatically. Folder-based bulk loading
and SD-card-backed soundfont demos (see `loaddata_examples.ino`) are not
re-implemented as menu entries and remain reachable the way they always
were: via MIDI CC (`z_config.ino`) or by editing `App_Setup()`
directly. See [`doc/sample_capacity.md`](sample_capacity.md) for how much
you can actually load.

## Why the Effects menu differs between RP2040 and RP2350

The phaser/vibrato/pitch-shifter/tremolo/reverb chain in `app.cpp` is
gated behind `REVERB_ENABLED` (a slightly misleading name - it gates the
*entire* chain, not just reverb). Turning it on allocates a ~60KB reverb
buffer on top of everything else.

- **RP2040 (Pico):** the "Generic RP2040" build already uses about 250KB of
  the chip's 264KB RAM once the sampler and delay buffers are in place (see
  [`board_info.md`](board_info.md)) - there is no room left for that extra
  buffer. `REVERB_ENABLED` is intentionally left **off** here, and the
  Effects menu only exposes **Delay**, matching what's actually wired into
  the audio path.
- **RP2350 (Pico 2):** with roughly 520KB of RAM and only ~320KB used by the
  tested build, there is comfortable headroom, so `REVERB_ENABLED` is
  turned **on** in `config/config_rp2350.h`. This also activates the
  matching MIDI CCs that were already defined (but previously inert) in
  `z_config.ino`.

If you free up RAM elsewhere on an RP2040 board (e.g. a smaller
`SAMPLER_STATIC_BUFFER_SAMPLE_CNT` or a smaller/no delay buffer), you can
turn `REVERB_ENABLED` on there too and the same menu entries will appear
automatically.

## Settings persistence

Every value adjusted through the menu is written to
`/.ui_settings.bin` on LittleFS, either explicitly via **System -> Save
Settings** or automatically ~3 seconds after the last change. On boot, if
that file exists, every saved value is restored and re-applied to the
corresponding effect/sampler parameter, so your settings survive a power
cycle. If no settings file exists yet, the menu just shows a neutral
starting position and nothing is touched until you actually adjust
something - identical to an untouched MIDI knob.

## Known limitations

- These parameter-setting functions have no getters, so the value shown on
  screen is the UI's own record of "last value it set" - it will not
  reflect changes made independently over MIDI until you touch that control
  in the menu (or reboot after saving).
- The UI runs on the RP2040/RP2350's second core
  (`App_Setup1()`/`App_Loop1()` in `app.cpp`) specifically so its OLED I2C
  traffic never blocks the real-time audio loop on core 0. This means
  parameter setters can run concurrently with MIDI/audio processing on core
  0 - the same tradeoff already accepted by this codebase's
  `ADC_ENABLED`/`MCP23_ENABLED` code for other boards. No additional locking
  is added.
- "Input Gain" only has an audible effect when `AUDIO_PASS_THROUGH` is
  defined (line-in monitoring), which is off by default in both RP2040 and
  RP2350 configs - it's kept in the menu because it's a genuine, documented
  app feature, but expect no change until you enable that mode.
