# How much sample data can I have?

There are two different limits, and they're often confused:

## 1. Flash storage (LittleFS) - where your `.wav`/`.sf2` files live

This is the filesystem partition you upload files to (see
[`doc/rp2040_arduino_ide_setup.md`](rp2040_arduino_ide_setup.md), step 7).
Per [`targets.csv`](../targets.csv) / [`board_info.md`](board_info.md):

| Board | Flash size | Sketch | Filesystem |
|---|---|---|---|
| Raspberry Pi Pico 2 (RP2350) | 4MB | 2MB | **2MB** |
| Generic RP2040 (8MB module) | 8MB | 4MB | **4MB** |

You can fit many files here (a few dozen short WAVs, or a handful of small
soundfonts) - this is not the tight constraint.

## 2. RAM sample pool - what's actually audible at once

The sampler copies decoded PCM audio into a RAM buffer for playback - **this
is the real limit**, and it's much smaller than the flash. On RP2040/RP2350
it's sized automatically at boot: the firmware starts with a large
allocation attempt and shrinks it (see `SAMPLER_RAM_BUFFER_MAX_SAMPLES` in
`config/config_rp2350.h`, and the loop in `App_Setup()` in `app.cpp`) until
`malloc()` succeeds, so it always uses whatever RAM is actually free after
everything else (reverb buffer, delay buffer, UI, audio buffers, stack).

**The exact number depends on your build** (which effects are enabled,
board, etc.) - check the Serial Monitor at boot, it prints the result:

```
Sampler RAM buffer: 123456 samples (246912 bytes, ~2.57s @ 48000Hz)
```

Rough expectations:

- **RP2350 (Pico 2):** the phaser/vibrato/tremolo/reverb chain is enabled
  (`REVERB_ENABLED`, ~60KB reverb buffer) but there's ~520KB of RAM total,
  so there's usually a meaningful amount left for samples - low hundreds of
  KB, i.e. a few seconds of 16-bit mono audio shared across everything
  you've loaded.
- **RP2040 (Pico):** uses a fixed `SAMPLER_STATIC_BUFFER_SAMPLE_CNT` (48K
  samples = 96KB = ~1.1s @ 44100Hz) instead of the dynamic pool, because
  there's only ~14KB of headroom left after the sampler + delay buffers on
  a 264KB-RAM chip (see `doc/oled_encoder_ui.md` for the full breakdown) -
  there isn't room to be more generous.

This is why the sampler is aimed at short one-shots/hits/stabs rather than
long loops or full songs - same tradeoff as most microcontroller-based
samplers. If you need much larger capacity, look at streaming samples from
storage instead of preloading them fully into RAM (not implemented here),
or SD card storage with on-demand loading (see the note about that in the
project discussion - not yet implemented).

## Loading only what you need

The OLED menu's **Load Sample** browser (see
[`doc/oled_encoder_ui.md`](oled_encoder_ui.md)) only loads a file into RAM
when you actually select it - browsing the file list itself doesn't consume
sample RAM. Use **Load Sample -> Clear All Samples** to free the pool before
loading something else if you're close to the limit.
