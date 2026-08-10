/*
 * Copyright (c) 2026 Marcel Licence
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Dieses Programm ist Freie Software: Sie können es unter den Bedingungen
 * der GNU General Public License, wie von der Free Software Foundation,
 * Version 3 der Lizenz oder (nach Ihrer Wahl) jeder neueren
 * veröffentlichten Version, weiter verteilen und/oder modifizieren.
 *
 * Dieses Programm wird in der Hoffnung bereitgestellt, dass es nützlich sein wird, jedoch
 * OHNE JEDE GEWÄHR,; sogar ohne die implizite
 * Gewähr der MARKTFÄHIGKEIT oder EIGNUNG FÜR EINEN BESTIMMTEN ZWECK.
 * Siehe die GNU General Public License für weitere Einzelheiten.
 *
 * Sie sollten eine Kopie der GNU General Public License zusammen mit diesem
 * Programm erhalten haben. Wenn nicht, siehe <https://www.gnu.org/licenses/>.
 */

/**
 * @file config.h
 * @author Marcel Licence
 * @date 21.11.2021
 *
 * @brief Configuration for RP2350 (Pi Pico 2)
 */


#if (defined ARDUINO_ARCH_RP2040) && (defined __ARM_FEATURE_DSP)
//#define MAX_DELAY 8096

#define SAMPLE_BUFFER_SIZE  48
#define SAMPLE_RATE  48000
#define PICO_AUDIO_I2S
#define PICO_AUDIO_I2S_DATA_PIN 26
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 27
#define MIDI_RX1_PIN    13
#define MIDI_TX1_PIN    12
//#define USE_ML_SYNTH_PRO /* needs the pro library */

/*
 * RP2350 has enough RAM headroom (~520KB total) to run the full
 * phaser/vibrato/pitch-shifter/tremolo/reverb chain alongside the sampler
 * buffer - unlike RP2040, see config_rp2040.h. This also activates the
 * matching MIDI CCs already defined in z_config.ino and the Effects menu
 * entries added by ui.cpp, see doc/oled_encoder_ui.md.
 */
#define REVERB_ENABLED

/*
 * Sample RAM pool: rather than guessing a fixed size at compile time, start
 * big and let app.cpp shrink the allocation until it fits whatever RAM is
 * actually left after the reverb/delay/UI buffers. See doc/sample_capacity.md.
 */
#define SAMPLER_RAM_BUFFER_MAX_SAMPLES  (1024 * 300)
#define SAMPLER_RAM_BUFFER_MIN_SAMPLES  (1024 * 8)

/*
 * SH1106 128x64 I2C OLED + rotary encoder (push-select) + 2 momentary buttons
 * See doc/oled_encoder_ui.md for wiring details and the full feature list.
 */
#define UI_OLED_ENABLED
#define UI_OLED_I2C_SDA_PIN     16
#define UI_OLED_I2C_SCL_PIN     17
#define UI_OLED_I2C_ADDR        0x3C

#define UI_ENC_A_PIN            10
#define UI_ENC_B_PIN            11
#define UI_ENC_SW_PIN           14

#define UI_BTN_BACK_PIN         15
#define UI_BTN_HOME_PIN         20

#if defined(PICO_DEFAULT_LED_PIN)
#define BLINK_LED_PIN PICO_DEFAULT_LED_PIN
#elif defined(LED_BUILTIN)
#define BLINK_LED_PIN LED_BUILTIN
//#define WS2812_PIN 3
#define STATUS_SIMPLE
//#define LED_COUNT 4
#else
#define BLINK_LED_PIN 25
#endif
#endif /* (defined ARDUINO_ARCH_RP2040) && (defined __ARM_FEATURE_DSP) */
