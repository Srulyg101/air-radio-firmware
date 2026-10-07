#include <string.h>
#include <stdlib.h>
#include "air_groups.h"
#include "audio.h"
#include "air_nv.h"
#include "misc.h"
#include "radio.h"
#include "mdc1200.h"

#define AIRGROUP_BASE 256u
#define AIRGROUP_MAGIC 0x47u
#define AIRGROUP_OPEN_ARG 0xA1u
#define AIRGROUP_CLOSE_ARG 0xA0u
#define AIRGROUP_MDC_OP 0x47u

typedef struct {
    uint8_t magic;
    uint8_t enabled;
    uint8_t channel;
    uint8_t reserved0;
    uint16_t group_id;
    uint8_t reserved1[2];
} __attribute__((packed)) AirGroupProfile;

static uint16_t open_mask;

static bool get_profile(unsigned int slot, AirGroupProfile *p)
{
    if (slot >= AIRGROUP_MAX_PROFILES || !p) return false;
    AIRNV_Read(AIRGROUP_BASE + slot * 8u, p, sizeof(*p));
    return p->magic == AIRGROUP_MAGIC && p->enabled == 1 &&
           p->group_id != 0 && p->group_id != 0xFFFFu;
}

static void save_profile(unsigned int slot, const AirGroupProfile *p)
{
    if (slot >= AIRGROUP_MAX_PROFILES || !p) return;
    AIRNV_Write(AIRGROUP_BASE + slot * 8u, p, sizeof(*p));
}

static int find_profile(uint8_t channel, uint16_t group_id)
{
    for (unsigned int i = 0; i < AIRGROUP_MAX_PROFILES; i++) {
        AirGroupProfile p;
        if (get_profile(i, &p) && p.channel == channel && p.group_id == group_id)
            return (int)i;
    }
    return -1;
}

static int find_free(void)
{
    for (unsigned int i = 0; i < AIRGROUP_MAX_PROFILES; i++) {
        AirGroupProfile p;
        if (!get_profile(i, &p)) return (int)i;
    }
    return -1;
}

static bool add_profile(uint8_t channel, uint16_t group_id)
{
    if (group_id == 0 || group_id == 0xFFFFu) return false;
    if (find_profile(channel, group_id) >= 0) return true;
    int slot = find_free();
    if (slot < 0) return false;

    AirGroupProfile p;
    memset(&p, 0xFF, sizeof(p));
    p.magic = AIRGROUP_MAGIC;
    p.enabled = 1;
    p.channel = channel;
    p.group_id = group_id;
    save_profile((unsigned int)slot, &p);
    return true;
}

static bool remove_profile(uint8_t channel, uint16_t group_id)
{
    const int slot = find_profile(channel, group_id);
    if (slot < 0) return false;
    uint8_t blank[8];
    memset(blank, 0xFF, sizeof(blank));
    AIRNV_Write(AIRGROUP_BASE + (unsigned int)slot * 8u, blank, sizeof(blank));
    open_mask &= ~(1u << slot);
    return true;
}

bool AIRGROUP_ChannelHasGroups(uint8_t channel)
{
    for (unsigned int i = 0; i < AIRGROUP_MAX_PROFILES; i++) {
        AirGroupProfile p;
        if (get_profile(i, &p) && p.channel == channel) return true;
    }
    return false;
}

uint16_t AIRGROUP_FirstGroupForChannel(uint8_t channel)
{
    for (unsigned int i = 0; i < AIRGROUP_MAX_PROFILES; i++) {
        AirGroupProfile p;
        if (get_profile(i, &p) && p.channel == channel) return p.group_id;
    }
    return 0;
}

bool AIRGROUP_ShouldUnmute(uint8_t channel)
{
    bool assigned = false;
    for (unsigned int i = 0; i < AIRGROUP_MAX_PROFILES; i++) {
        AirGroupProfile p;
        if (!get_profile(i, &p) || p.channel != channel) continue;
        assigned = true;
        if (open_mask & (1u << i)) return true;
    }
    return !assigned;
}

static void apply_audio_for_current_channel(void)
{
    if (!gRxVfo) return;
    const uint8_t channel = gRxVfo->CHANNEL_SAVE;
    if (AIRGROUP_ShouldUnmute(channel)) {
        AUDIO_AudioPathOn();
        gEnableSpeaker = true;
    } else {
        AUDIO_AudioPathOff();
        gEnableSpeaker = false;
    }
}

void AIRGROUP_OnMdcPacket(uint8_t op, uint8_t arg, uint16_t id)
{
    if (op != AIRGROUP_MDC_OP || (arg != AIRGROUP_OPEN_ARG && arg != AIRGROUP_CLOSE_ARG))
        return;

    for (unsigned int i = 0; i < AIRGROUP_MAX_PROFILES; i++) {
        AirGroupProfile p;
        if (!get_profile(i, &p) || p.group_id != id) continue;
        if (arg == AIRGROUP_OPEN_ARG) open_mask |= (1u << i);
        else open_mask &= ~(1u << i);
    }

    apply_audio_for_current_channel();
    gBeepToPlay = (arg == AIRGROUP_OPEN_ARG) ? BEEP_MDC_CALL_ALERT : BEEP_MDC_POST_ID;
    gUpdateDisplay = true;
    gUpdateStatus = true;
}

static bool parse_dec4(const char *s, uint16_t *out)
{
    if (!s || !out) return false;
    uint16_t v = 0;
    for (unsigned int i = 0; i < 4; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = (uint16_t)(v * 10u + (uint16_t)(s[i] - '0'));
    }
    if (v == 0) return false;
    *out = v;
    return true;
}

static bool current_mr_channel(uint8_t *out)
{
    if (!out || !gTxVfo) return false;
    if (gTxVfo->CHANNEL_SAVE > MR_CHANNEL_LAST) return false;
    *out = gTxVfo->CHANNEL_SAVE;
    return true;
}

bool AIRGROUP_HandleLocalCode(const char *code)
{
    if (!code) return false;
    const size_t n = strlen(code);
    uint16_t group = 0;
    uint8_t channel = 0;

    // 71GGGG = assign GGGG to the current memory channel
    if (n == 6 && code[0] == '7' && code[1] == '1' &&
        parse_dec4(code + 2, &group) && current_mr_channel(&channel)) {
        const bool ok = add_profile(channel, group);
        gBeepToPlay = ok ? BEEP_1KHZ_60MS_OPTIONAL : BEEP_500HZ_60MS_DOUBLE_BEEP;
        return true;
    }

    // 70GGGG = remove GGGG from the current memory channel
    if (n == 6 && code[0] == '7' && code[1] == '0' &&
        parse_dec4(code + 2, &group) && current_mr_channel(&channel)) {
        const bool ok = remove_profile(channel, group);
        gBeepToPlay = ok ? BEEP_1KHZ_60MS_OPTIONAL : BEEP_500HZ_60MS_DOUBLE_BEEP;
        return true;
    }

    // 72GGGG = dispatcher opens group GGGG over MDC
    if (n == 6 && code[0] == '7' && code[1] == '2' && parse_dec4(code + 2, &group)) {
        MDC1200_send_group_control(group, true);
        return true;
    }

    // 73GGGG = dispatcher closes group GGGG over MDC
    if (n == 6 && code[0] == '7' && code[1] == '3' && parse_dec4(code + 2, &group)) {
        MDC1200_send_group_control(group, false);
        return true;
    }

    // 75CCCGGGG = assign GGGG to explicit memory channel CCC (001..200)
    if (n == 9 && code[0] == '7' && code[1] == '5') {
        if (code[2] < '0' || code[2] > '9' || code[3] < '0' || code[3] > '9' || code[4] < '0' || code[4] > '9')
            return false;
        const unsigned int ch = (unsigned int)(code[2]-'0')*100u + (unsigned int)(code[3]-'0')*10u + (unsigned int)(code[4]-'0');
        if (ch < 1 || ch > (unsigned int)MR_CHANNEL_LAST + 1u || !parse_dec4(code + 5, &group))
            return false;
        const bool ok = add_profile((uint8_t)(ch - 1u), group);
        gBeepToPlay = ok ? BEEP_1KHZ_60MS_OPTIONAL : BEEP_500HZ_60MS_DOUBLE_BEEP;
        return true;
    }

    // 76CCCGGGG = remove GGGG from explicit memory channel CCC
    if (n == 9 && code[0] == '7' && code[1] == '6') {
        if (code[2] < '0' || code[2] > '9' || code[3] < '0' || code[3] > '9' || code[4] < '0' || code[4] > '9')
            return false;
        const unsigned int ch = (unsigned int)(code[2]-'0')*100u + (unsigned int)(code[3]-'0')*10u + (unsigned int)(code[4]-'0');
        if (ch < 1 || ch > (unsigned int)MR_CHANNEL_LAST + 1u || !parse_dec4(code + 5, &group))
            return false;
        const bool ok = remove_profile((uint8_t)(ch - 1u), group);
        gBeepToPlay = ok ? BEEP_1KHZ_60MS_OPTIONAL : BEEP_500HZ_60MS_DOUBLE_BEEP;
        return true;
    }

    return false;
}

void AIRGROUP_Init(void)
{
    // Group gates always boot closed. Dispatcher must open them after power-up.
    open_mask = 0;
}
