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
 */

/**
 * @file ui.cpp
 * @author Marcel Licence
 * @date 09.08.2026
 *
 * @brief   On-device UI for RP2040/RP2350: SH1106 128x64 I2C OLED, a rotary encoder
 * @n       with push-select, and two momentary buttons (Back/Home).
 * @n
 * @n       This drives a hierarchical menu that exposes sample loading (browsed
 * @n       live from LittleFS), sampler parameters, all wired-up effects and the
 * @n       input gain - the same parameters otherwise only reachable via MIDI CC
 * @n       (see z_config.ino). Values are persisted to LittleFS so they survive
 * @n       a power cycle.
 * @n
 * @n       See doc/oled_encoder_ui.md for wiring and a full feature list.
 * @n
 * @n       Runs on the second core (App_Setup1()/App_Loop1(), see app.cpp) so the
 * @n       OLED I2C traffic never blocks the real-time audio loop on core 0.
 * @n       Because of that, parameter setters here run concurrently with core 0
 * @n       (MIDI processing / audio rendering) touching the same modules - the
 * @n       same tradeoff already accepted by the existing ADC_ENABLED/MCP23_ENABLED
 * @n       code in App_Loop1() for other boards.
 */

#ifdef __CDT_PARSER__
#include <cdt.h>
#endif


#include "config.h"


#ifdef UI_OLED_ENABLED

#include "ui.h"

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <string.h>
#include <stdio.h>

#include <FS.h>
#include <LittleFS.h>

#include <ml_sampler.h>
#include <ml_phaser.h>
#include <ml_delay_q.h>

#include "app.h"
#include "wav_to_sampler.h"
#include "sf_to_sampler.h"
#include <fs/fs_access.h>


/*
 * pin fallbacks (config.h normally provides these, see doc/oled_encoder_ui.md)
 */
#ifndef UI_OLED_I2C_SDA_PIN
#define UI_OLED_I2C_SDA_PIN 16
#endif
#ifndef UI_OLED_I2C_SCL_PIN
#define UI_OLED_I2C_SCL_PIN 17
#endif
#ifndef UI_OLED_I2C_ADDR
#define UI_OLED_I2C_ADDR 0x3C
#endif
#ifndef UI_ENC_A_PIN
#define UI_ENC_A_PIN 10
#endif
#ifndef UI_ENC_B_PIN
#define UI_ENC_B_PIN 11
#endif
#ifndef UI_ENC_SW_PIN
#define UI_ENC_SW_PIN 14
#endif
#ifndef UI_BTN_BACK_PIN
#define UI_BTN_BACK_PIN 15
#endif
#ifndef UI_BTN_HOME_PIN
#define UI_BTN_HOME_PIN 20
#endif


#define UI_DEBOUNCE_MS              25
#define UI_RENDER_INTERVAL_MS       33  /* ~30fps */
#define UI_TOAST_MS                 1200
#define UI_SETTINGS_AUTOSAVE_MS     3000
#define UI_MENU_STACK_MAX           4
#define UI_MENU_VISIBLE_ROWS        5

#define UI_SETTINGS_FILE            "/.ui_settings.bin"
#define UI_SETTINGS_MAGIC           0x55A5

#define UI_LOAD_WAV_ALL_NOTES       0
#define UI_LOAD_SF2_SAMPLES         1
#define UI_LOAD_SF2_INSTR_MULTI     2
#define UI_LOAD_SF2_FULL            3


/*
 * SH1106 1.3" 128x64 OLED, full frame buffer, hardware I2C (see doc/oled_encoder_ui.md)
 */
static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);


/*
 * ---------------------------------------------------------------------------
 * menu data model
 * ---------------------------------------------------------------------------
 */
typedef void (*UiParamSetter)(uint8_t param, uint8_t value);
typedef void (*UiAction)(void);

enum UiItemType
{
    UI_SUBMENU = 0,
    UI_VALUE,
    UI_ACTION,
    UI_FILE_LOADER,
    UI_INFO
};

struct UiMenu; /* forward declaration, submenu items point back to this */

struct UiItem
{
    const char *name;
    UiItemType type;
    const UiMenu *submenu;      /* UI_SUBMENU */
    UiParamSetter setter;       /* UI_VALUE */
    uint8_t setterParam;        /* UI_VALUE */
    uint8_t *value;              /* UI_VALUE, points into s_paramValue[] */
    uint8_t minValue;            /* UI_VALUE */
    uint8_t maxValue;            /* UI_VALUE */
    uint8_t step;                /* UI_VALUE */
    UiAction action;             /* UI_ACTION */
    const char *fileExt;         /* UI_FILE_LOADER, e.g. ".wav" */
    uint8_t loaderMode;          /* UI_FILE_LOADER */
    uint8_t infoId;               /* UI_INFO */
};

struct UiMenu
{
    const char *title;
    const UiItem *items;
    uint8_t count;
};

#define UI_ITEM_SUBMENU(nm, sub) \
    { nm, UI_SUBMENU, sub, NULL, 0, NULL, 0, 0, 0, NULL, NULL, 0, 0 }

#define UI_ITEM_VALUE(nm, fn, prm, ptr, mn, mx, stp) \
    { nm, UI_VALUE, NULL, fn, prm, ptr, mn, mx, stp, NULL, NULL, 0, 0 }

#define UI_ITEM_ACTION(nm, fn) \
    { nm, UI_ACTION, NULL, NULL, 0, NULL, 0, 0, 0, fn, NULL, 0, 0 }

#define UI_ITEM_FILE(nm, ext, mode) \
    { nm, UI_FILE_LOADER, NULL, NULL, 0, NULL, 0, 0, 0, NULL, ext, mode, 0 }

#define UI_ITEM_INFO(nm, iid) \
    { nm, UI_INFO, NULL, NULL, 0, NULL, 0, 0, 0, NULL, NULL, 0, iid }


/*
 * ---------------------------------------------------------------------------
 * persisted parameter shadow values
 *
 * These functions are write-only (no getter exists), so this array is the
 * UI's own record of "last value it set". On boot it is restored from
 * LittleFS (if a settings file exists) and every restored value is re-applied
 * via its setter so saved effect/sampler settings actually take effect. If no
 * settings file exists yet, values stay at a neutral display default and the
 * setters are *not* called - leaving whatever built-in default app.cpp
 * already compiled in, exactly like an untouched MIDI knob would.
 * ---------------------------------------------------------------------------
 */
enum UiParamId
{
    UI_P_INPUT_GAIN = 0,
    UI_P_SAMPLER_TUNE_COARSE,
    UI_P_SAMPLER_HOLD,
    UI_P_SAMPLER_RELEASE,
    UI_P_SAMPLER_RELEASE_SAMPLE,
#ifdef MAX_DELAY_Q
    UI_P_DELAY_LEVEL,
    UI_P_DELAY_FEEDBACK,
    UI_P_DELAY_LENGTH,
#endif
#ifdef REVERB_ENABLED
    UI_P_LFO_SPEED,
    UI_P_PHASER_DEPTH,
    UI_P_PHASER_G,
    UI_P_VIBRATO_DEPTH,
    UI_P_VIBRATO_INTENSITY,
    UI_P_PITCH_SPEED,
    UI_P_PITCH_MIX,
    UI_P_PITCH_FEEDBACK,
    UI_P_TREMOLO_DEPTH,
    UI_P_REVERB_LEVEL,
#endif
    UI_P_COUNT
};

static uint8_t s_paramValue[UI_P_COUNT];


/*
 * ---------------------------------------------------------------------------
 * action wrappers (menu actions must be void(void))
 * ---------------------------------------------------------------------------
 */
static void UiAction_LoopEntireSample(void)
{
    Sampler_LoopEntireSample(0, 127);
}

static bool s_testNoteOn = false;
static void UiAction_ToggleTestNote(void)
{
    if (s_testNoteOn)
    {
        Sampler_NoteOff(0, 69);
        s_testNoteOn = false;
    }
    else
    {
        Sampler_NoteOn(0, 69, 127);
        s_testNoteOn = true;
    }
}

static void UI_SaveSettings(void);
static void UiAction_SaveSettings(void)
{
    UI_SaveSettings();
}

static void UiAction_NoOp(void)
{
}


/*
 * ---------------------------------------------------------------------------
 * menu tree
 * ---------------------------------------------------------------------------
 */
static const UiItem s_loadItems[] =
{
    UI_ITEM_ACTION("Clear All Samples", Sampler_ClearAllSamples),
    UI_ITEM_FILE("Load WAV->AllKeys",   ".wav", UI_LOAD_WAV_ALL_NOTES),
    UI_ITEM_FILE("Load SF2 Samples",    ".sf2", UI_LOAD_SF2_SAMPLES),
    UI_ITEM_FILE("Load SF2 Instr.",     ".sf2", UI_LOAD_SF2_INSTR_MULTI),
    UI_ITEM_FILE("Load Full SF2",       ".sf2", UI_LOAD_SF2_FULL),
};
static const UiMenu s_loadMenu = { "Load Sample", s_loadItems, sizeof(s_loadItems) / sizeof(s_loadItems[0]) };

static const UiItem s_samplerItems[] =
{
    UI_ITEM_ACTION("Prev Preset", Sampler_DecSample),
    UI_ITEM_ACTION("Next Preset", Sampler_IncSample),
    UI_ITEM_VALUE("Tune Coarse", Sampler_TuneCoarse, 0, &s_paramValue[UI_P_SAMPLER_TUNE_COARSE], 0, 127, 1),
    UI_ITEM_VALUE("Hold", Sampler_ChangeParameter, SAMPLER_PARAM_HOLD, &s_paramValue[UI_P_SAMPLER_HOLD], 0, 127, 1),
    UI_ITEM_VALUE("Release", Sampler_ChangeParameter, SAMPLER_PARAM_RELEASE, &s_paramValue[UI_P_SAMPLER_RELEASE], 0, 127, 1),
    UI_ITEM_VALUE("Release (Smpl)", Sampler_ChangeParameterSample, SAMPLER_PARAM_RELEASE, &s_paramValue[UI_P_SAMPLER_RELEASE_SAMPLE], 0, 127, 1),
    UI_ITEM_ACTION("Loop Entire Smpl", UiAction_LoopEntireSample),
    UI_ITEM_ACTION("All Notes Off", Sampler_AllNotesOff),
    UI_ITEM_ACTION("Test Note A4", UiAction_ToggleTestNote),
};
static const UiMenu s_samplerMenu = { "Sampler", s_samplerItems, sizeof(s_samplerItems) / sizeof(s_samplerItems[0]) };

static const UiItem s_effectsItems[] =
{
#ifdef MAX_DELAY_Q
    UI_ITEM_VALUE("Delay Level", DelayQ_SetOutputLevel, 0, &s_paramValue[UI_P_DELAY_LEVEL], 0, 127, 1),
    UI_ITEM_VALUE("Delay Feedback", DelayQ_SetFeedback, 0, &s_paramValue[UI_P_DELAY_FEEDBACK], 0, 127, 1),
    UI_ITEM_VALUE("Delay Length", App_DelayQ_SetLength, 0, &s_paramValue[UI_P_DELAY_LENGTH], 0, 127, 1),
#endif
#ifdef REVERB_ENABLED
    UI_ITEM_VALUE("LFO Speed", Lfo1_SetSpeed, 0, &s_paramValue[UI_P_LFO_SPEED], 0, 127, 1),
    UI_ITEM_VALUE("Phaser Depth", Phaser_SetDepth, 0, &s_paramValue[UI_P_PHASER_DEPTH], 0, 127, 1),
    UI_ITEM_VALUE("Phaser G", Phaser_SetG, 0, &s_paramValue[UI_P_PHASER_G], 0, 127, 1),
    UI_ITEM_VALUE("Vibrato Depth", AppVibrato_SetDepth, 0, &s_paramValue[UI_P_VIBRATO_DEPTH], 0, 127, 1),
    UI_ITEM_VALUE("Vibrato Intens.", AppVibrato_SetIntensity, 0, &s_paramValue[UI_P_VIBRATO_INTENSITY], 0, 127, 1),
    UI_ITEM_VALUE("Pitch Speed", PitchShifter_SetSpeed, 0, &s_paramValue[UI_P_PITCH_SPEED], 0, 127, 1),
    UI_ITEM_VALUE("Pitch Mix", PitchShifter_SetMix, 0, &s_paramValue[UI_P_PITCH_MIX], 0, 127, 1),
    UI_ITEM_VALUE("Pitch Feedback", PitchShifter_SetFeedback, 0, &s_paramValue[UI_P_PITCH_FEEDBACK], 0, 127, 1),
    UI_ITEM_VALUE("Tremolo Depth", AppTremolo_SetDepth, 0, &s_paramValue[UI_P_TREMOLO_DEPTH], 0, 127, 1),
    UI_ITEM_VALUE("Reverb Level", AppReverb_SetLevel, 0, &s_paramValue[UI_P_REVERB_LEVEL], 0, 127, 1),
#endif
#if !defined(MAX_DELAY_Q) && !defined(REVERB_ENABLED)
    UI_ITEM_ACTION("(no effects built)", UiAction_NoOp),
#endif
};
static const UiMenu s_effectsMenu = { "Effects", s_effectsItems, sizeof(s_effectsItems) / sizeof(s_effectsItems[0]) };

static const UiItem s_systemItems[] =
{
    UI_ITEM_ACTION("Save Settings", UiAction_SaveSettings),
    UI_ITEM_INFO("Pin Info", 0),
    UI_ITEM_INFO("Memory Info", 1),
};
static const UiMenu s_systemMenu = { "System", s_systemItems, sizeof(s_systemItems) / sizeof(s_systemItems[0]) };

static const UiItem s_rootItems[] =
{
    UI_ITEM_SUBMENU("Load Sample", &s_loadMenu),
    UI_ITEM_SUBMENU("Sampler", &s_samplerMenu),
    UI_ITEM_SUBMENU("Effects", &s_effectsMenu),
    UI_ITEM_VALUE("Input Gain", AppSetInputGain, 0, &s_paramValue[UI_P_INPUT_GAIN], 0, 127, 1),
    UI_ITEM_SUBMENU("System", &s_systemMenu),
};
static const UiMenu s_rootMenu = { "Menu", s_rootItems, sizeof(s_rootItems) / sizeof(s_rootItems[0]) };


/*
 * ---------------------------------------------------------------------------
 * navigation state
 * ---------------------------------------------------------------------------
 */
enum UiState
{
    UI_STATE_HOME = 0,
    UI_STATE_MENU,
    UI_STATE_EDIT_VALUE,
    UI_STATE_FILE_BROWSE,
    UI_STATE_INFO,
    UI_STATE_TOAST
};

struct UiMenuStackEntry
{
    const UiMenu *menu;
    uint8_t selected;
};

static UiState s_state = UI_STATE_HOME;
static UiMenuStackEntry s_menuStack[UI_MENU_STACK_MAX];
static uint8_t s_menuStackDepth = 0;
static const UiItem *s_editItem = NULL;
static uint8_t s_infoId = 0;

static char s_toastMsg[24];
static uint32_t s_toastStartMs = 0;
static UiState s_toastReturnState = UI_STATE_HOME;

struct UiFileBrowseState
{
    const UiItem *item;
    char ext[8];
    uint32_t count;
    uint32_t index;
    char filename[64];
    bool valid;
};
static UiFileBrowseState s_browse;

static bool s_settingsDirty = false;
static uint32_t s_lastChangeMs = 0;
static uint32_t s_lastRenderMs = 0;


/*
 * ---------------------------------------------------------------------------
 * encoder + buttons
 * ---------------------------------------------------------------------------
 */
static int8_t UI_Encoder_ReadStep(void)
{
    static uint8_t state = 0;
    static int8_t accum = 0;
    static const int8_t table[16] =
    {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
        0, 1, -1, 0
    };

    uint8_t a = digitalRead(UI_ENC_A_PIN);
    uint8_t b = digitalRead(UI_ENC_B_PIN);
    state = (uint8_t)(((state << 2) | (a << 1) | b) & 0x0F);
    accum = (int8_t)(accum + table[state]);

    int8_t step = 0;
    if (accum >= 4)
    {
        step = 1;
        accum = 0;
    }
    else if (accum <= -4)
    {
        step = -1;
        accum = 0;
    }
    return step;
}

struct UiButton
{
    uint8_t pin;
    uint8_t lastStable;
    uint8_t lastReading;
    uint32_t lastChangeMs;
};

static UiButton s_btnSelect = { UI_ENC_SW_PIN, HIGH, HIGH, 0 };
static UiButton s_btnBack   = { UI_BTN_BACK_PIN, HIGH, HIGH, 0 };
static UiButton s_btnHome   = { UI_BTN_HOME_PIN, HIGH, HIGH, 0 };

/* returns true on the debounced press edge (active low, INPUT_PULLUP) */
static bool UI_Button_Pressed(UiButton *btn)
{
    uint8_t reading = digitalRead(btn->pin);
    uint32_t now = millis();

    if (reading != btn->lastReading)
    {
        btn->lastChangeMs = now;
        btn->lastReading = reading;
    }

    bool pressedEdge = false;
    if ((now - btn->lastChangeMs) > UI_DEBOUNCE_MS)
    {
        if (btn->lastStable != reading)
        {
            btn->lastStable = reading;
            if (reading == LOW)
            {
                pressedEdge = true;
            }
        }
    }
    return pressedEdge;
}


/*
 * ---------------------------------------------------------------------------
 * settings persistence
 * ---------------------------------------------------------------------------
 */
static void UI_ApplyAllValues(const UiMenu *menu)
{
    for (uint8_t i = 0; i < menu->count; i++)
    {
        const UiItem *item = &menu->items[i];
        if (item->type == UI_VALUE)
        {
            item->setter(item->setterParam, *item->value);
        }
        else if (item->type == UI_SUBMENU)
        {
            UI_ApplyAllValues(item->submenu);
        }
    }
}

static void UI_SaveSettings(void)
{
    File f = LittleFS.open(UI_SETTINGS_FILE, "w");
    if (!f)
    {
        Serial.printf("UI: failed to open %s for writing\n", UI_SETTINGS_FILE);
        return;
    }

    uint16_t magic = UI_SETTINGS_MAGIC;
    uint8_t count = UI_P_COUNT;
    f.write((const uint8_t *)&magic, sizeof(magic));
    f.write(&count, 1);
    f.write(s_paramValue, UI_P_COUNT);
    f.close();

    s_settingsDirty = false;
    Serial.printf("UI: settings saved (%u params)\n", count);
}

static void UI_LoadSettings(void)
{
    File f = LittleFS.open(UI_SETTINGS_FILE, "r");
    if (!f)
    {
        Serial.printf("UI: no saved settings, using defaults\n");
        return;
    }

    uint16_t magic = 0;
    uint8_t count = 0;
    f.read((uint8_t *)&magic, sizeof(magic));
    f.read(&count, 1);

    bool ok = (magic == UI_SETTINGS_MAGIC) && (count == UI_P_COUNT);
    uint8_t buf[UI_P_COUNT];
    if (ok)
    {
        uint32_t n = f.read(buf, UI_P_COUNT);
        ok = (n == UI_P_COUNT);
    }
    f.close();

    if (!ok)
    {
        Serial.printf("UI: settings file invalid/outdated, using defaults\n");
        return;
    }

    memcpy(s_paramValue, buf, UI_P_COUNT);
    UI_ApplyAllValues(&s_rootMenu);
    Serial.printf("UI: settings restored (%u params)\n", count);
}

static void UI_SettingsAutosave(void)
{
    if (s_settingsDirty && ((millis() - s_lastChangeMs) > UI_SETTINGS_AUTOSAVE_MS))
    {
        UI_SaveSettings();
    }
}


/*
 * ---------------------------------------------------------------------------
 * file loading (browsed live from LittleFS via the fs_access helpers)
 * ---------------------------------------------------------------------------
 */
static void UI_LoadFile(uint8_t loaderMode, const char *filename)
{
    Serial.printf("UI: loading %s\n", filename);
    switch (loaderMode)
    {
    case UI_LOAD_WAV_ALL_NOTES:
        WavToSmpl_FileToSingleNote(FS_ID_LITTLEFS, filename, W2S_ALL_NOTES);
        Sampler_InstrumentDone();
        break;
    case UI_LOAD_SF2_SAMPLES:
        SF2ToSmpl_LoadAllSamplesFromSF(FS_ID_LITTLEFS, filename);
        break;
    case UI_LOAD_SF2_INSTR_MULTI:
        SF2ToSmpl_LoadAllInstrumentsMultiFromSF(FS_ID_LITTLEFS, filename);
        break;
    case UI_LOAD_SF2_FULL:
        SF2ToSmpl_LoadCompleteSoundFont(FS_ID_LITTLEFS, filename);
        break;
    default:
        break;
    }
}

static void UI_RefreshBrowseFilename(void)
{
    s_browse.valid = false;
    if (s_browse.count == 0)
    {
        return;
    }

    char filter[8];
    strncpy(filter, s_browse.ext, sizeof(filter) - 1);
    filter[sizeof(filter) - 1] = '\0';

    if (getFileFromIdx(s_browse.index, s_browse.filename, filter))
    {
        s_browse.valid = true;
    }
}

static void UI_StartFileBrowse(const UiItem *item)
{
    s_browse.item = item;
    strncpy(s_browse.ext, item->fileExt, sizeof(s_browse.ext) - 1);
    s_browse.ext[sizeof(s_browse.ext) - 1] = '\0';

    char filter[8];
    strncpy(filter, s_browse.ext, sizeof(filter) - 1);
    filter[sizeof(filter) - 1] = '\0';

    s_browse.count = getFileCount(filter);
    s_browse.index = 0;
    UI_RefreshBrowseFilename();

    s_state = UI_STATE_FILE_BROWSE;
}


/*
 * ---------------------------------------------------------------------------
 * toast overlay
 * ---------------------------------------------------------------------------
 */
static void UI_ShowToast(const char *msg, UiState returnState)
{
    strncpy(s_toastMsg, msg, sizeof(s_toastMsg) - 1);
    s_toastMsg[sizeof(s_toastMsg) - 1] = '\0';
    s_toastStartMs = millis();
    s_toastReturnState = returnState;
    s_state = UI_STATE_TOAST;
}


/*
 * ---------------------------------------------------------------------------
 * input handling per state
 * ---------------------------------------------------------------------------
 */
static void UI_HandleMenuInput(int8_t encStep, bool selectEdge, bool backEdge)
{
    UiMenuStackEntry *top = &s_menuStack[s_menuStackDepth - 1];

    if (encStep != 0)
    {
        int16_t idx = (int16_t)top->selected + encStep;
        if (idx < 0)
        {
            idx = (int16_t)top->menu->count - 1;
        }
        else if (idx >= (int16_t)top->menu->count)
        {
            idx = 0;
        }
        top->selected = (uint8_t)idx;
    }

    if (backEdge)
    {
        if (s_menuStackDepth > 1)
        {
            s_menuStackDepth--;
        }
        else
        {
            s_state = UI_STATE_HOME;
        }
        return;
    }

    if (selectEdge && (top->menu->count > 0))
    {
        const UiItem *item = &top->menu->items[top->selected];
        switch (item->type)
        {
        case UI_SUBMENU:
            if (s_menuStackDepth < UI_MENU_STACK_MAX)
            {
                s_menuStack[s_menuStackDepth].menu = item->submenu;
                s_menuStack[s_menuStackDepth].selected = 0;
                s_menuStackDepth++;
            }
            break;

        case UI_VALUE:
            s_editItem = item;
            s_state = UI_STATE_EDIT_VALUE;
            break;

        case UI_ACTION:
            item->action();
            UI_ShowToast(item->name, UI_STATE_MENU);
            break;

        case UI_FILE_LOADER:
            UI_StartFileBrowse(item);
            break;

        case UI_INFO:
            s_infoId = item->infoId;
            s_state = UI_STATE_INFO;
            break;
        }
    }
}

static void UI_HandleEditInput(int8_t encStep, bool selectEdge, bool backEdge)
{
    if (encStep != 0)
    {
        int16_t val = (int16_t)(*s_editItem->value) + (int16_t)(encStep * (int16_t)s_editItem->step);
        if (val < s_editItem->minValue)
        {
            val = s_editItem->minValue;
        }
        if (val > s_editItem->maxValue)
        {
            val = s_editItem->maxValue;
        }
        *s_editItem->value = (uint8_t)val;
        s_editItem->setter(s_editItem->setterParam, *s_editItem->value);
        s_settingsDirty = true;
        s_lastChangeMs = millis();
    }

    if (selectEdge || backEdge)
    {
        s_state = UI_STATE_MENU;
    }
}

static void UI_HandleFileBrowseInput(int8_t encStep, bool selectEdge, bool backEdge)
{
    if (backEdge)
    {
        s_state = UI_STATE_MENU;
        return;
    }

    if (s_browse.count == 0)
    {
        if (selectEdge)
        {
            s_state = UI_STATE_MENU;
        }
        return;
    }

    if (encStep != 0)
    {
        int32_t idx = (int32_t)s_browse.index + encStep;
        if (idx < 0)
        {
            idx = (int32_t)s_browse.count - 1;
        }
        if ((uint32_t)idx >= s_browse.count)
        {
            idx = 0;
        }
        s_browse.index = (uint32_t)idx;
        UI_RefreshBrowseFilename();
    }

    if (selectEdge && s_browse.valid)
    {
        UI_LoadFile(s_browse.item->loaderMode, s_browse.filename);
        UI_ShowToast("Loaded", UI_STATE_MENU);
    }
}


/*
 * ---------------------------------------------------------------------------
 * rendering
 * ---------------------------------------------------------------------------
 */
static void UI_DrawTruncated(int x, int y, const char *text)
{
    char buf[40];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    while ((strlen(buf) > 1) && (u8g2.getStrWidth(buf) > (128 - x)))
    {
        buf[strlen(buf) - 1] = '\0';
    }
    u8g2.drawStr(x, y, buf);
}

static void UI_RenderHome(void)
{
    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawStr(0, 12, "ML Sampler");
    u8g2.drawHLine(0, 15, 128);

    u8g2.setFont(u8g2_font_6x10_tr);
    char line[24];
    snprintf(line, sizeof(line), "Samples: %u", (unsigned)Sampler_GetSampleCount());
    u8g2.drawStr(0, 30, line);

    uint32_t used = Sampler_GetUsedSpace();
    uint32_t maxSpace = Sampler_GetMaxSpace();
    uint32_t pct = maxSpace ? (uint32_t)(((uint64_t)used * 100u) / maxSpace) : 0;
    snprintf(line, sizeof(line), "Mem used: %lu%%", (unsigned long)pct);
    u8g2.drawStr(0, 42, line);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 62, "Push=Menu  Home=Status");
}

static void UI_RenderMenu(void)
{
    const UiMenuStackEntry *top = &s_menuStack[s_menuStackDepth - 1];
    const UiMenu *menu = top->menu;

    u8g2.setFont(u8g2_font_6x10_tr);
    UI_DrawTruncated(0, 9, menu->title);
    u8g2.drawHLine(0, 11, 128);

    if (menu->count == 0)
    {
        u8g2.drawStr(0, 26, "(empty)");
        return;
    }

    uint8_t windowStart = 0;
    if (menu->count > UI_MENU_VISIBLE_ROWS)
    {
        if (top->selected > 2)
        {
            windowStart = top->selected - 2;
        }
        uint8_t maxStart = menu->count - UI_MENU_VISIBLE_ROWS;
        if (windowStart > maxStart)
        {
            windowStart = maxStart;
        }
    }

    uint8_t visibleCount = menu->count - windowStart;
    if (visibleCount > UI_MENU_VISIBLE_ROWS)
    {
        visibleCount = UI_MENU_VISIBLE_ROWS;
    }

    for (uint8_t i = 0; i < visibleCount; i++)
    {
        uint8_t idx = windowStart + i;
        int rowTop = 13 + (i * 10);
        int baseline = rowTop + 8;

        if (idx == top->selected)
        {
            u8g2.setDrawColor(1);
            u8g2.drawBox(0, rowTop, 128, 10);
            u8g2.setDrawColor(0);
            UI_DrawTruncated(2, baseline, menu->items[idx].name);
            u8g2.setDrawColor(1);
        }
        else
        {
            UI_DrawTruncated(2, baseline, menu->items[idx].name);
        }
    }
}

static void UI_RenderEditValue(void)
{
    u8g2.setFont(u8g2_font_6x10_tr);
    UI_DrawTruncated(0, 9, s_editItem->name);
    u8g2.drawHLine(0, 11, 128);

    char valStr[8];
    snprintf(valStr, sizeof(valStr), "%3u", *s_editItem->value);
    u8g2.setFont(u8g2_font_9x18B_tr);
    u8g2.drawStr(44, 38, valStr);

    uint8_t range = (s_editItem->maxValue > s_editItem->minValue) ? (uint8_t)(s_editItem->maxValue - s_editItem->minValue) : 1;
    uint8_t pos = (uint8_t)(*s_editItem->value - s_editItem->minValue);
    int filled = (int)(((uint32_t)pos * 98u) / range);

    u8g2.drawFrame(14, 46, 100, 10);
    if (filled > 0)
    {
        u8g2.drawBox(15, 47, filled, 8);
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 62, "Turn=Adjust Push/Back=Done");
}

static void UI_RenderFileBrowse(void)
{
    u8g2.setFont(u8g2_font_6x10_tr);
    UI_DrawTruncated(0, 9, s_browse.item->name);
    u8g2.drawHLine(0, 11, 128);

    if (s_browse.count == 0)
    {
        u8g2.drawStr(0, 30, "No matching files");
        u8g2.setFont(u8g2_font_5x7_tr);
        char line[24];
        snprintf(line, sizeof(line), "(%s) on LittleFS", s_browse.ext);
        u8g2.drawStr(0, 40, line);
    }
    else
    {
        char line[24];
        snprintf(line, sizeof(line), "File %lu / %lu", (unsigned long)(s_browse.index + 1), (unsigned long)s_browse.count);
        u8g2.drawStr(0, 26, line);
        UI_DrawTruncated(0, 40, s_browse.valid ? s_browse.filename : "?");
    }

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 62, "Push=Load  Back=Cancel");
}

static void UI_RenderInfo(void)
{
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 9, s_infoId == 0 ? "Pin Info" : "Memory Info");
    u8g2.drawHLine(0, 11, 128);

    u8g2.setFont(u8g2_font_5x7_tr);
    char line[28];

    if (s_infoId == 0)
    {
        snprintf(line, sizeof(line), "OLED SDA:%d SCL:%d", UI_OLED_I2C_SDA_PIN, UI_OLED_I2C_SCL_PIN);
        u8g2.drawStr(0, 20, line);
        snprintf(line, sizeof(line), "Enc A:%d B:%d SW:%d", UI_ENC_A_PIN, UI_ENC_B_PIN, UI_ENC_SW_PIN);
        u8g2.drawStr(0, 29, line);
        snprintf(line, sizeof(line), "Btn Back:%d Home:%d", UI_BTN_BACK_PIN, UI_BTN_HOME_PIN);
        u8g2.drawStr(0, 38, line);
#if defined(MIDI_RX1_PIN) && defined(MIDI_TX1_PIN)
        snprintf(line, sizeof(line), "MIDI RX:%d TX:%d", MIDI_RX1_PIN, MIDI_TX1_PIN);
        u8g2.drawStr(0, 47, line);
#elif defined(MIDI_RX2_PIN)
        snprintf(line, sizeof(line), "MIDI2 RX:%d", MIDI_RX2_PIN);
        u8g2.drawStr(0, 47, line);
#endif
    }
    else
    {
        snprintf(line, sizeof(line), "Samples: %u", (unsigned)Sampler_GetSampleCount());
        u8g2.drawStr(0, 20, line);
        snprintf(line, sizeof(line), "Used:  %lu", (unsigned long)Sampler_GetUsedSpace());
        u8g2.drawStr(0, 29, line);
        snprintf(line, sizeof(line), "Free:  %lu", (unsigned long)Sampler_GetFreeSpace());
        u8g2.drawStr(0, 38, line);
        snprintf(line, sizeof(line), "Total: %lu", (unsigned long)Sampler_GetMaxSpace());
        u8g2.drawStr(0, 47, line);
    }

    u8g2.drawStr(0, 62, "Push/Back=Return");
}

static void UI_RenderToast(void)
{
    u8g2.setDrawColor(1);
    u8g2.drawFrame(4, 22, 120, 22);
    u8g2.drawBox(5, 23, 118, 20);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_6x10_tr);
    int w = u8g2.getStrWidth(s_toastMsg);
    int x = (128 - w) / 2;
    if (x < 6)
    {
        x = 6;
    }
    u8g2.drawStr(x, 36, s_toastMsg);
    u8g2.setDrawColor(1);
}

static void UI_Render(void)
{
    u8g2.clearBuffer();

    switch (s_state)
    {
    case UI_STATE_HOME:
        UI_RenderHome();
        break;
    case UI_STATE_MENU:
        UI_RenderMenu();
        break;
    case UI_STATE_EDIT_VALUE:
        UI_RenderEditValue();
        break;
    case UI_STATE_FILE_BROWSE:
        UI_RenderFileBrowse();
        break;
    case UI_STATE_INFO:
        UI_RenderInfo();
        break;
    case UI_STATE_TOAST:
        UI_RenderToast();
        break;
    }

    u8g2.sendBuffer();
}


/*
 * ---------------------------------------------------------------------------
 * public API
 * ---------------------------------------------------------------------------
 */
void UI_Setup(void)
{
    for (uint8_t i = 0; i < UI_P_COUNT; i++)
    {
        s_paramValue[i] = 64; /* neutral display default, see comment above s_paramValue */
    }

    pinMode(UI_ENC_A_PIN, INPUT_PULLUP);
    pinMode(UI_ENC_B_PIN, INPUT_PULLUP);
    pinMode(UI_ENC_SW_PIN, INPUT_PULLUP);
    pinMode(UI_BTN_BACK_PIN, INPUT_PULLUP);
    pinMode(UI_BTN_HOME_PIN, INPUT_PULLUP);

    /* I2C pins must be set before Wire/u8g2 init, see doc/oled_encoder_ui.md */
    Wire.setSDA(UI_OLED_I2C_SDA_PIN);
    Wire.setSCL(UI_OLED_I2C_SCL_PIN);
    Wire.setClock(400000);

    u8g2.begin();

    Serial.printf("UI: SH1106 OLED on I2C SDA=%d SCL=%d, encoder A=%d B=%d SW=%d, buttons Back=%d Home=%d\n",
                   UI_OLED_I2C_SDA_PIN, UI_OLED_I2C_SCL_PIN,
                   UI_ENC_A_PIN, UI_ENC_B_PIN, UI_ENC_SW_PIN,
                   UI_BTN_BACK_PIN, UI_BTN_HOME_PIN);

    UI_LoadSettings();

    s_state = UI_STATE_HOME;
    s_menuStackDepth = 0;
}

void UI_Loop(void)
{
    int8_t encStep = UI_Encoder_ReadStep();
    bool selectEdge = UI_Button_Pressed(&s_btnSelect);
    bool backEdge = UI_Button_Pressed(&s_btnBack);
    bool homeEdge = UI_Button_Pressed(&s_btnHome);

    if (homeEdge)
    {
        s_menuStackDepth = 0;
        s_state = UI_STATE_HOME;
    }
    else
    {
        switch (s_state)
        {
        case UI_STATE_HOME:
            if (selectEdge)
            {
                s_menuStack[0].menu = &s_rootMenu;
                s_menuStack[0].selected = 0;
                s_menuStackDepth = 1;
                s_state = UI_STATE_MENU;
            }
            break;

        case UI_STATE_MENU:
            UI_HandleMenuInput(encStep, selectEdge, backEdge);
            break;

        case UI_STATE_EDIT_VALUE:
            UI_HandleEditInput(encStep, selectEdge, backEdge);
            break;

        case UI_STATE_FILE_BROWSE:
            UI_HandleFileBrowseInput(encStep, selectEdge, backEdge);
            break;

        case UI_STATE_INFO:
            if (selectEdge || backEdge)
            {
                s_state = UI_STATE_MENU;
            }
            break;

        case UI_STATE_TOAST:
            if (((millis() - s_toastStartMs) > UI_TOAST_MS) || selectEdge || backEdge)
            {
                s_state = s_toastReturnState;
            }
            break;
        }
    }

    uint32_t now = millis();
    if ((now - s_lastRenderMs) >= UI_RENDER_INTERVAL_MS)
    {
        s_lastRenderMs = now;
        UI_Render();
    }

    UI_SettingsAutosave();
}


#else /* UI_OLED_ENABLED */

#include "ui.h"

void UI_Setup(void)
{
}

void UI_Loop(void)
{
}

#endif /* UI_OLED_ENABLED */
