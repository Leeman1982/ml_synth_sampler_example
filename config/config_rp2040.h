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
 * @file config_rp2040.h
 * @author Marcel Licence
 *
 * @brief Configuration for
 * Board: "Rapsberry Pi Pico"
 *
 * BCK: 26
 * DIN: 28
 * LCK: 27  (always BCK + 1)
 *
 * MIDI_RX: 12 (GP9)
 *
 * Pinout @see https://www.raspberrypi-spy.co.uk/2021/01/pi-pico-pinout-and-power-pins/#prettyPhoto
 */

#if (defined ARDUINO_ARCH_RP2040) && (!defined __ARM_FEATURE_DSP)


// #define BOARD_ML_PICO_SYNTH

#define SAMPLE_HDR_CNT  3
#define RP2040_AUDIO_PWM
#define MAX_DELAY_Q 24000
#define SAMPLER_STATIC_BUFFER_SAMPLE_CNT   (1024 * 48)


#ifdef ARDUINO_RASPBERRY_PI_PICO
#define BLINK_LED_PIN LED_BUILTIN
#else
#define BLINK_LED_PIN 19
#endif


#define MIDI_RX2_PIN    5
#define MIDI_PORT2_ACTIVE

#define MIDI_USB_ENABLED /* connect RP2040 as a USB device */

#if 1
#define RP2040_AUDIO_PWM
#else
#define PICO_AUDIO_I2S
#define PICO_AUDIO_I2S_DATA_PIN 26
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 27
#endif

#define SAMPLE_BUFFER_SIZE  48
#define SAMPLE_RATE  44100


/*
 * SH1106 128x64 I2C OLED + rotary encoder (push-select) + 2 momentary buttons
 * See doc/oled_encoder_ui.md for wiring details and the full feature list.
 *
 * NOTE: the phaser/vibrato/tremolo/reverb chain (REVERB_ENABLED) is
 * intentionally left disabled on RP2040 - there isn't enough RAM headroom
 * left after the sampler + delay buffers (see doc/board_info.md). The
 * Effects menu therefore only exposes Delay on this board. RP2350 (Pico 2)
 * has enough RAM and enables the full chain, see config_rp2350.h.
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


#endif /* (defined ARDUINO_ARCH_RP2040) && (!defined __ARM_FEATURE_DSP) */
