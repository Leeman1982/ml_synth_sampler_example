# Flashing the sampler on Raspberry Pi Pico / Pico 2 (RP2040 / RP2350) with the Arduino IDE

This project already contains ready-to-use configuration for RP2040 (Raspberry Pi
Pico / generic RP2040 boards) and RP2350 (Raspberry Pi Pico 2), see
[`config/config_rp2040.h`](../config/config_rp2040.h) and
[`config/config_rp2350.h`](../config/config_rp2350.h). The board is selected
automatically at compile time from the board/architecture you choose in the
Arduino IDE — you do not need to edit any source file to switch between Pico
and Pico 2.

The tested combinations below match [`targets.csv`](../targets.csv) and
[`doc/board_info.md`](board_info.md).

## What you need

- A Raspberry Pi Pico, Pico 2, or another RP2040/RP2350 board (e.g. a
  "Generic RP2040" board with more flash)
- A USB cable (data-capable, not charge-only)
- [Arduino IDE 2.x](https://www.arduino.cc/en/software)
- For audio out:
  - Pico (RP2040, default config): PWM audio output — a simple RC low-pass
    filter (and amp) on the output pin(s) is enough, no external DAC required
  - Pico 2 (RP2350, default config): I2S audio output — an I2S DAC such as a
    PCM5102A is required
- Optional: a MIDI source (USB-MIDI host for RP2040, or a hardware serial MIDI
  input for RP2350) to actually trigger notes/load sounds

## 1. Install the RP2040/RP2350 board core

1. Open Arduino IDE → `File` → `Preferences`.
2. Add this URL to **Additional Boards Manager URLs**:
   ```
   https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
   ```
3. Open `Tools` → `Board` → `Boards Manager`, search for **"Raspberry Pi Pico/RP2040"**
   (by Earle F. Philhower, III) and install it. Version **4.0.1 or newer** is
   known to work (see [`board_info.md`](board_info.md)).

## 2. Install ML_SynthTools and ML_SynthTools_Lib

Both are required and are precompiled libraries with RP2040/RP2350 binaries
already included, so no extra toolchain setup is needed for them:

1. Download [ML_SynthTools](https://github.com/marcel-licence/ML_SynthTools)
   and [ML_SynthTools_Lib](https://github.com/marcel-licence/ML_SynthTools_Lib)
   as ZIP files (green **Code** button → **Download ZIP**).
2. In the Arduino IDE: `Sketch` → `Include Library` → `Add .ZIP Library...`
   and select each ZIP in turn.

## 3. Get the sampler project

Download or clone this repository, then open
[`ml_synth_sampler_example.ino`](../ml_synth_sampler_example.ino) in the
Arduino IDE (this is the sketch's main entry point — `app.cpp` contains the
actual application code and is compiled alongside it).

## 4. Select your board and match the tested settings

### Raspberry Pi Pico 2 (RP2350)

`Tools` → `Board` → `Raspberry Pi Pico/RP2040` → **Raspberry Pi Pico 2**

| Setting        | Value                             |
|----------------|------------------------------------|
| Flash Size     | 4MB (Sketch: 2MB, FS: 2MB)         |
| CPU Speed      | 150 MHz                            |
| Optimize       | Optimize Even More (-O3)           |
| USB Stack      | Pico SDK                           |
| Upload Method  | Default (UF2)                      |

### Generic RP2040 board (more flash, e.g. 8/16MB modules)

`Tools` → `Board` → `Raspberry Pi Pico/RP2040` → **Generic RP2040**

| Setting        | Value                                    |
|----------------|-------------------------------------------|
| Flash Size     | 8MB (Sketch: 4MB, FS: 4MB)                 |
| CPU Speed      | 133 MHz                                    |
| Optimize       | Optimize Even More (-O3)                   |
| USB Stack      | Adafruit TinyUSB                           |
| Upload Method  | Default (UF2)                              |

### Plain Raspberry Pi Pico (RP2040, 2MB flash)

`Tools` → `Board` → `Raspberry Pi Pico/RP2040` → **Raspberry Pi Pico**

Not part of the automated build matrix, but supported by
`config_rp2040.h` (it checks for `ARDUINO_RASPBERRY_PI_PICO` to use the
onboard LED). Pick a **Flash Size** split that leaves enough room for your
sample data in the filesystem partition (e.g. "2MB (Sketch: 1MB, FS: 1MB)").

## 5. Wiring / audio output

- **Pico / RP2040 (PWM audio, default):** No DAC is required — the audio
  pin(s) are printed to the Serial Monitor on boot (see step 8). Add a simple
  RC low-pass filter and an amplifier/headphone stage on that pin.
  If you want I2S output instead on an RP2040, edit
  [`config/config_rp2040.h`](../config/config_rp2040.h) and switch the
  `#if 1` / `#else` block to use `PICO_AUDIO_I2S` (data pin 26, clock base
  pin 27) instead of `RP2040_AUDIO_PWM`.
- **Pico 2 / RP2350 (I2S audio, default):** Wire an I2S DAC with `DATA` on
  GPIO 26 and `BCK`/`LCK` starting at GPIO 27 (`PICO_AUDIO_I2S_CLOCK_PIN_BASE`).
- **MIDI:**
  - RP2040: USB-MIDI device class is enabled by default (just plug into a
    computer/host), plus a second hardware serial MIDI input on GPIO 5.
  - RP2350: hardware serial MIDI on GPIO 13 (RX) / GPIO 12 (TX).

## 6. First upload (entering the bootloader)

If the board has never run an Arduino/Pico SDK sketch before, put it in USB
mass-storage bootloader mode first:

1. Hold the **BOOTSEL** button.
2. Plug in the USB cable (or press **RESET** while still holding BOOTSEL).
3. Release BOOTSEL — the board shows up as a USB drive.
4. In the Arduino IDE, select the matching serial/UF2 port under `Tools` →
   `Port`, then click **Upload**.

Subsequent uploads work directly over the USB serial port without needing
BOOTSEL, as long as the previously flashed sketch is still running normally.

## 7. Upload the sample data (LittleFS)

The demo sample in [`data/`](../data) (and any WAV/SF2 files you add there)
needs to be uploaded to the board's LittleFS filesystem partition — this is
separate from uploading the sketch itself.

For Arduino IDE 2.x, install the
[**arduino-littlefs-upload**](https://github.com/earlephilhower/arduino-littlefs-upload)
plugin (drop the release `.vsix` into your Arduino IDE `plugins` folder,
inside the IDE's user data directory). After installing it and restarting the
IDE, open the Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`) and run
**"Upload LittleFS to Pico/ESP8266/ESP32"** with the sketch open. This
packages everything in the sketch's `data/` folder into the FS partition
sized according to the Flash Size setting chosen in step 4.

## 8. Verify over the Serial Monitor

Open `Tools` → `Serial Monitor` at **115200 baud** after uploading. On boot
the sketch prints the resolved pin configuration for audio/MIDI, PSRAM/flash
info if applicable, `Loading data`, and finally `setup done!` once
initialization completes successfully.

## 9. Loading and playing samples

By default no sample is auto-loaded on boot. Look at
[`loaddata_examples.ino`](../loaddata_examples.ino) and
[`z_config.ino`](../z_config.ino) for the different loading strategies (single
WAV to all keys, WAV folder to keys/presets, SoundFont samples/instruments) and
how they're wired to MIDI CC messages. Send the mapped MIDI CC (or add a
direct call in `App_Setup()` in `app.cpp`, e.g.
`WavToSmpl_FileToSingleNote(FS_ID_LITTLEFS, "/PappRohrSample.wav", W2S_ALL_NOTES);`)
to load the bundled demo sample, then play notes via MIDI.

## Troubleshooting

- **"Precompiled library ... not found" / undefined reference errors:** make
  sure both ML_SynthTools and ML_SynthTools_Lib are installed and match the
  versions referenced in [`README.md`](../README.md); see the main
  [ML_SynthTools README](https://github.com/marcel-licence/ML_SynthTools#compiling-note)
  for the general precompiled-library troubleshooting steps.
- **Board not detected after upload:** re-enter BOOTSEL mode (step 6) and
  re-select the port.
- **No sound:** confirm you picked the audio path matching your wiring (PWM
  vs I2S, see step 5) and check the Serial Monitor pin printout.
