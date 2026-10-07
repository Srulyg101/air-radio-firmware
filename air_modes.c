#include <string.h>
#include "air_modes.h"
#include "air_config.h"
#include "audio.h"
#include "driver/backlight.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"

static bool up_pressed;
static uint8_t saved_tx_vfo;
static bool tx_override_active;

static void apply_mode(void)
{
    const uint8_t mode = AIRCFG_GetMode();
    if (mode == AIR_MODE_21 || mode == AIR_MODE_23) {
        gEeprom.DUAL_WATCH = DUAL_WATCH_CHAN_A;
        gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
        gEeprom.TX_VFO = 0;
        gEeprom.RX_VFO = 0;
        RADIO_SelectVfos();
        gFlagReconfigureVfos = true;
        gUpdateStatus = true;
        gUpdateDisplay = true;
    }
}

void AIRMODES_Init(void)
{
    up_pressed = false;
    tx_override_active = false;
    apply_mode();
}

unsigned int AIRMODES_GetMode(void)
{
    return AIRCFG_GetMode();
}

bool AIRMODES_IsShabbos(void)
{
    return AIRCFG_GetShabbos();
}

bool AIRMODES_HandleLocalCode(const char *code)
{
    if (!code) return false;

    if (strcmp(code, "20") == 0) {
        AIRCFG_SetMode(AIR_MODE_NORMAL);
        gEeprom.DUAL_WATCH = DUAL_WATCH_OFF;
        gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
        gEeprom.TX_VFO = 0;
        RADIO_SelectVfos();
        gFlagReconfigureVfos = true;
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
        return true;
    }

    if (strcmp(code, "21") == 0) {
        AIRCFG_SetMode(AIR_MODE_21);
        apply_mode();
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
        return true;
    }

    if (strcmp(code, "23") == 0) {
        AIRCFG_SetMode(AIR_MODE_23);
        apply_mode();
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
        return true;
    }

    if (strcmp(code, "99") == 0) {
        AIRCFG_SetShabbos(!AIRCFG_GetShabbos());
        if (AIRCFG_GetShabbos())
            BACKLIGHT_TurnOff();
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
        gUpdateStatus = true;
        gUpdateDisplay = true;
        return true;
    }

    if (strcmp(code, "98") == 0) {
        AIRCFG_SetShabbos(false);
        gUpdateStatus = true;
        gUpdateDisplay = true;
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
        return true;
    }

    return false;
}

void AIRMODES_ObserveKey(KEY_Code_t key, bool pressed, bool held)
{
    if (key != KEY_UP) return;
    if (pressed) up_pressed = true;
    else if (!held) up_pressed = false;
}

void AIRMODES_BeforePtt(void)
{
    const uint8_t mode = AIRCFG_GetMode();
    if (mode != AIR_MODE_21 && mode != AIR_MODE_23) return;

    saved_tx_vfo = gEeprom.TX_VFO;
    tx_override_active = true;

    if (mode == AIR_MODE_23 && up_pressed)
        gEeprom.TX_VFO = 0;
    else
        gEeprom.TX_VFO = 1;

    RADIO_SelectVfos();
}

void AIRMODES_AfterPtt(void)
{
    if (!tx_override_active) return;
    gEeprom.TX_VFO = saved_tx_vfo;
    if (AIRCFG_GetMode() == AIR_MODE_21 || AIRCFG_GetMode() == AIR_MODE_23)
        gEeprom.TX_VFO = 0;
    RADIO_SelectVfos();
    tx_override_active = false;
    gFlagReconfigureVfos = true;
}
