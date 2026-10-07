#include <string.h>
#include "mdc1200.h"
#include "air_config.h"
#include "air_groups.h"
#include "audio.h"
#include "driver/bk4819.h"
#include "air_nv.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/ui.h"

#define MDC_CONTACT_BASE 0u
#define MDC_CONTACT_MAGIC 0xA5u

typedef struct {
    uint16_t id;
    uint8_t magic;
    char name[10];
    uint8_t reserved[3];
} __attribute__((packed)) MDCContactRecord;

static const uint8_t mdc_sync[5] = {0x07, 0x09, 0x2A, 0x44, 0x6F};
static uint8_t sync_xor[5];

uint8_t mdc1200_op;
uint8_t mdc1200_arg;
uint16_t mdc1200_unit_id;
uint8_t mdc1200_rx_ready_tick_500ms;

static uint8_t rx_buffer[5 + (MDC1200_FEC_K * 2)];
static uint8_t rx_index;
static bool rx_sync_negative;

static MDC1200_Event history[MDC1200_HISTORY_SIZE];
static uint8_t history_count;
static uint8_t history_head;
static uint8_t history_selected;
static uint16_t tick500;
static bool log_open;
static bool edit_mode;
static char edit_name[11];
static uint8_t edit_pos;
static KEY_Code_t edit_last_key = KEY_INVALID;

static uint16_t last_id;
static uint8_t last_op;
static uint8_t last_arg;
static uint16_t last_tick;

static uint16_t crc16_mdc(const void *data, unsigned int len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0;
    while (len--) {
        crc ^= *p++;
        for (unsigned int i = 0; i < 8; i++)
            crc = (crc & 1u) ? (crc >> 1) ^ 0x8408u : (crc >> 1);
    }
    return crc ^ 0xFFFFu;
}

static void xor_modulation(void *data, unsigned int size)
{
    uint8_t *p = (uint8_t *)data;
    uint8_t prev = 0;
    for (unsigned int i = 0; i < size; i++) {
        uint8_t in = p[i], out = 0;
        for (int bit = 7; bit >= 0; bit--) {
            const uint8_t now = (in >> bit) & 1u;
            if (now != prev) out |= 1u << bit;
            prev = now;
        }
        p[i] = out ^ 0xFFu;
    }
}

static void error_correction(uint8_t *data)
{
    uint8_t shift = 0, syn = 0;
    for (int i = 0; i < MDC1200_FEC_K; i++) {
        const uint8_t bi = data[i];
        for (int bit = 0; bit < 8; bit++) {
            unsigned int k = 0;
            shift = (shift << 1) | ((bi >> bit) & 1u);
            const uint8_t b = ((shift >> 6) ^ (shift >> 5) ^ (shift >> 2) ^ shift) & 1u;
            syn = (syn << 1) | (((b ^ (data[i + MDC1200_FEC_K] >> bit)) & 1u) ? 1u : 0u);
            if (syn & 0x80) k++;
            if (syn & 0x20) k++;
            if (syn & 0x04) k++;
            if (syn & 0x02) k++;
            if (k >= 3) {
                int ii = i;
                int bn = bit - 7;
                if (bn < 0) { bn += 8; ii--; }
                if (ii >= 0) data[ii] ^= 1u << bn;
                syn ^= 0xA6;
            }
        }
    }
}

static bool decode_data(uint8_t *data)
{
    uint8_t bits[(MDC1200_FEC_K * 2) * 8];
    unsigned int k = 0;
    for (unsigned int i = 0; i < 16; i++) {
        for (unsigned int m = 0; m < MDC1200_FEC_K; m++) {
            const unsigned int n = (m * 16) + i;
            bits[k++] = (data[n >> 3] >> ((7 - n) & 7u)) & 1u;
        }
    }
    k = 0;
    for (unsigned int i = 0; i < MDC1200_FEC_K * 2; i++) {
        uint8_t b = 0;
        for (unsigned int j = 0; j < 8; j++)
            if (bits[k++]) b |= 1u << j;
        data[i] = b;
    }
    error_correction(data);
    const uint16_t want = ((uint16_t)data[5] << 8) | data[4];
    return crc16_mdc(data, 4) == want;
}

static uint8_t *encode_data(uint8_t *data)
{
    uint8_t shift = 0;
    for (unsigned int i = 0; i < MDC1200_FEC_K; i++) {
        const uint8_t bi = data[i];
        uint8_t bo = 0;
        for (unsigned int bit = 0; bit < 8; bit++) {
            shift = (shift << 1) | ((bi >> bit) & 1u);
            bo |= (((shift >> 6) ^ (shift >> 5) ^ (shift >> 2) ^ shift) & 1u) << bit;
        }
        data[MDC1200_FEC_K + i] = bo;
    }

    uint8_t interleaved[(MDC1200_FEC_K * 2) * 8];
    unsigned int k = 0;
    for (unsigned int i = 0; i < MDC1200_FEC_K * 2; i++) {
        const uint8_t b = data[i];
        for (unsigned int bit = 0; bit < 8; bit++) {
            interleaved[k] = (b >> bit) & 1u;
            k += 16;
            if (k >= sizeof(interleaved)) k -= sizeof(interleaved) - 1;
        }
    }
    k = 0;
    for (unsigned int i = 0; i < MDC1200_FEC_K * 2; i++) {
        uint8_t b = 0;
        for (int bit = 7; bit >= 0; bit--)
            if (interleaved[k++]) b |= 1u << bit;
        data[i] = b;
    }
    return data + (MDC1200_FEC_K * 2);
}

static unsigned int encode_packet(uint8_t *data, uint8_t op, uint8_t arg, uint16_t id)
{
    uint8_t *p = data;
    memset(p, 0, 3);
    p += 3;
    memcpy(p, mdc_sync, sizeof(mdc_sync));
    p += sizeof(mdc_sync);
    p[0] = op;
    p[1] = arg;
    p[2] = id >> 8;
    p[3] = id & 0xFF;
    const uint16_t crc = crc16_mdc(p, 4);
    p[4] = crc & 0xFF;
    p[5] = crc >> 8;
    p[6] = 0;
    p = encode_data(p);
    const unsigned int size = p - data;
    xor_modulation(data, size);
    return size;
}

static bool contact_get(unsigned int slot, MDCContactRecord *rec)
{
    if (slot >= MDC1200_MAX_CONTACTS || rec == 0) return false;
    AIRNV_Read(MDC_CONTACT_BASE + slot * 16u, rec, sizeof(*rec));
    if (rec->magic != MDC_CONTACT_MAGIC || rec->id == 0 || rec->id == 0xFFFFu) return false;
    rec->name[9] = 0;
    return true;
}

static bool contact_find(uint16_t id, char *name, int *slot_out)
{
    for (unsigned int i = 0; i < MDC1200_MAX_CONTACTS; i++) {
        MDCContactRecord rec;
        if (!contact_get(i, &rec)) continue;
        if (rec.id == id) {
            if (name) {
                memcpy(name, rec.name, 10);
                name[10] = 0;
                for (int j = 9; j >= 0 && (name[j] == ' ' || name[j] == 0xFF); j--) name[j] = 0;
            }
            if (slot_out) *slot_out = i;
            return true;
        }
    }
    if (name) name[0] = 0;
    if (slot_out) *slot_out = -1;
    return false;
}

static void contact_save(uint16_t id, const char *name)
{
    int slot = -1;
    contact_find(id, 0, &slot);
    if (slot < 0) {
        for (unsigned int i = 0; i < MDC1200_MAX_CONTACTS; i++) {
            MDCContactRecord rec;
            if (!contact_get(i, &rec)) { slot = i; break; }
        }
    }
    if (slot < 0) slot = 0;

    MDCContactRecord rec;
    memset(&rec, 0xFF, sizeof(rec));
    rec.id = id;
    rec.magic = MDC_CONTACT_MAGIC;
    memset(rec.name, ' ', sizeof(rec.name));
    if (name) {
        unsigned int n = strlen(name);
        if (n > sizeof(rec.name)) n = sizeof(rec.name);
        memcpy(rec.name, name, n);
    }
    EEPROM_WriteBuffer(MDC_CONTACT_BASE + slot * 16u, (uint8_t *)&rec);
    EEPROM_WriteBuffer(MDC_CONTACT_BASE + slot * 16u + 8u, ((uint8_t *)&rec) + 8);
}

const char *MDC1200_event_name(uint8_t op, uint8_t arg)
{
    if (op == 0x00 && arg == 0x81) return "EMERGENCY";
    if (op == 0x01 && arg == 0x80) return "PTT ID";
    if (op == 0x01 && arg == 0x00) return "POST ID";
    if (op == 0x11 && arg == 0x8A) return "REMOTE MON";
    if (op == 0x22 && arg == 0x06) return "STATUS REQ";
    if (op == 0x2B && arg == 0x0C) return "RADIO ENABLE";
    if (op == 0x2B && arg == 0x00) return "RADIO DISABL";
    if (op == 0x35 && arg == 0x89) return "CALL ALERT";
    if (op == 0x46) return "STATUS";
    if (op == 0x47) return "MESSAGE";
    if (op == 0x63 && arg == 0x85) return "RADIO CHECK";
    return "MDC UNKNOWN";
}

static MDC1200_Event *selected_event(void)
{
    if (history_count == 0) return 0;
    if (history_selected >= history_count) history_selected = history_count - 1;
    const unsigned int idx = (history_head + MDC1200_HISTORY_SIZE - 1u - history_selected) % MDC1200_HISTORY_SIZE;
    return &history[idx];
}

static void record_event(uint16_t id, uint8_t op, uint8_t arg)
{
    if (id == last_id && op == last_op && arg == last_arg && (uint16_t)(tick500 - last_tick) <= 3u) {
        mdc1200_rx_ready_tick_500ms = 12;
        log_open = true;
        history_selected = 0;
        gUpdateDisplay = true;
        return;
    }

    last_id = id;
    last_op = op;
    last_arg = arg;
    last_tick = tick500;

    MDC1200_Event *e = &history[history_head];
    e->unit_id = id;
    e->op = op;
    e->arg = arg;
    e->channel = gRxVfo ? gRxVfo->CHANNEL_SAVE : 0xFF;
    e->frequency = gRxVfo ? gRxVfo->pRX->Frequency : 0;
    e->tick = tick500;
    history_head = (history_head + 1u) % MDC1200_HISTORY_SIZE;
    if (history_count < MDC1200_HISTORY_SIZE) history_count++;
    history_selected = 0;
    log_open = true;
    mdc1200_rx_ready_tick_500ms = 12;
    gUpdateDisplay = true;

    AIRGROUP_OnMdcPacket(op, arg, id);

    if (op == 0x35 && arg == 0x89)
        gBeepToPlay = BEEP_MDC_CALL_ALERT;
    else if (op == 0x00 && arg == 0x81)
        gBeepToPlay = BEEP_MDC_EMERGENCY;
    else if (op == 0x63 && arg == 0x85)
        gBeepToPlay = BEEP_MDC_RADIO_CHECK;
    else if (op == 0x46)
        gBeepToPlay = BEEP_MDC_STATUS;
    else if (op == 0x47 && arg != 0xA1 && arg != 0xA0)
        gBeepToPlay = BEEP_MDC_MESSAGE;
    else if (op == 0x01 && arg == 0x80)
        gBeepToPlay = BEEP_MDC_PTT_ID;
    else if (op == 0x01 && arg == 0x00)
        gBeepToPlay = BEEP_MDC_POST_ID;
    else if (op == 0x11 && arg == 0x8A)
        gBeepToPlay = BEEP_MDC_REMOTE_MONITOR;
}

static bool process_rx_data(const uint8_t *buffer, unsigned int size, uint8_t *op, uint8_t *arg, uint16_t *id)
{
    struct {
        uint8_t bit, xor_bit;
        uint64_t shift;
        unsigned int bit_count, stage, data_index;
        bool inverted;
        uint8_t data[40];
    } r;
    memset(&r, 0, sizeof(r));

    for (unsigned int index = 0; index < size; index++) {
        const uint8_t rb = buffer[index];
        for (int bit = 7; bit >= 0; bit--) {
            r.bit = (rb >> bit) & 1u;
            r.xor_bit = (r.xor_bit ^ r.bit) & 1u;
            r.shift = (r.shift << 1) | r.xor_bit;
            r.bit_count++;

            if (r.stage == 0) {
                if (r.bit_count >= 40) {
                    uint64_t normal = 0x07092A446FULL ^ r.shift;
                    uint64_t inverse = (0xFFFFFFFFFFULL ^ 0x07092A446FULL) ^ r.shift;
                    unsigned int nc = 0, ic = 0;
                    for (unsigned int i = 0; i < 40; i++) {
                        nc += normal & 1u; normal >>= 1;
                        ic += inverse & 1u; inverse >>= 1;
                    }
                    nc = 40 - nc;
                    ic = 40 - ic;
                    if (nc >= 32 || ic >= 32) {
                        r.inverted = ic > nc;
                        r.bit_count = 0;
                        r.data_index = 0;
                        r.stage = 1;
                    }
                }
                continue;
            }

            if (r.bit_count < 8) continue;
            r.bit_count = 0;
            if (r.data_index < sizeof(r.data))
                r.data[r.data_index++] = r.shift & 0xFFu;
            if (r.data_index < MDC1200_FEC_K * 2) continue;

            if (!decode_data(r.data)) return false;
            *op = r.data[0];
            *arg = r.data[1];
            *id = ((uint16_t)r.data[2] << 8) | r.data[3];
            return true;
        }
    }
    return false;
}

void MDC1200_enable_rx(bool enable)
{
    if (!enable) {
        BK4819_WriteRegister(BK4819_REG_70, 0);
        BK4819_WriteRegister(BK4819_REG_58, 0);
        return;
    }

    const uint16_t reg59 = (1u << 3);
    BK4819_WriteRegister(BK4819_REG_70, (1u << 7) | 96u);
    BK4819_WriteRegister(BK4819_REG_72, (uint16_t)((1200u * 1353245u + (1u << 16)) >> 17));
    BK4819_WriteRegister(BK4819_REG_58,
        (1u << 13) | (7u << 10) | (3u << 8) | (1u << 1) | 1u);
    BK4819_WriteRegister(BK4819_REG_5A, ((uint16_t)sync_xor[1] << 8) | sync_xor[2]);
    BK4819_WriteRegister(BK4819_REG_5B, ((uint16_t)sync_xor[3] << 8) | sync_xor[4]);
    BK4819_WriteRegister(BK4819_REG_5C, 0x5625);
    BK4819_WriteRegister((BK4819_REGISTER_t)0x5E, (64u << 3) | 1u);
    BK4819_WriteRegister(BK4819_REG_5D, ((MDC1200_FEC_K * 2 - 1u) << 8));
    BK4819_WriteRegister(BK4819_REG_59, (1u << 15) | (1u << 14) | reg59);
    BK4819_WriteRegister(BK4819_REG_59, (1u << 12) | reg59);
    BK4819_WriteRegister(BK4819_REG_02, 0);
}

void MDC1200_process_rx(uint16_t interrupt_bits)
{
    const uint16_t flags = BK4819_ReadRegister(BK4819_REG_0B);
    const uint16_t reg59 = BK4819_ReadRegister(BK4819_REG_59) & ~((1u << 15) | (1u << 14) | (1u << 12) | (1u << 11));
    const bool neg = (flags & (1u << 7)) != 0;

    if (interrupt_bits & BK4819_REG_02_FSK_RX_SYNC) {
        rx_index = 0;
        rx_sync_negative = neg;
        memset(rx_buffer, 0, sizeof(rx_buffer));
        for (unsigned int i = 0; i < sizeof(sync_xor); i++)
            rx_buffer[rx_index++] = sync_xor[i] ^ (neg ? 0xFFu : 0u);
    }

    if (interrupt_bits & BK4819_REG_02_FSK_FIFO_ALMOST_FULL) {
        const unsigned int count = BK4819_ReadRegister((BK4819_REGISTER_t)0x5E) & 7u;
        for (unsigned int i = 0; i < count; i++) {
            const uint16_t word = BK4819_ReadRegister(BK4819_REG_5F) ^ (rx_sync_negative ? 0xFFFFu : 0u);
            if (rx_index < sizeof(rx_buffer)) rx_buffer[rx_index++] = word & 0xFF;
            if (rx_index < sizeof(rx_buffer)) rx_buffer[rx_index++] = word >> 8;
        }

        if (rx_index >= sizeof(rx_buffer)) {
            BK4819_WriteRegister(BK4819_REG_59, (1u << 15) | (1u << 14) | reg59);
            BK4819_WriteRegister(BK4819_REG_59, (1u << 12) | reg59);
            if (process_rx_data(rx_buffer, rx_index, &mdc1200_op, &mdc1200_arg, &mdc1200_unit_id))
                record_event(mdc1200_unit_id, mdc1200_op, mdc1200_arg);
            rx_index = 0;
        }
    }

    if (interrupt_bits & BK4819_REG_02_FSK_RX_FINISHED) {
        rx_index = 0;
        BK4819_WriteRegister(BK4819_REG_59, (1u << 15) | (1u << 14) | reg59);
        BK4819_WriteRegister(BK4819_REG_59, (1u << 12) | reg59);
    }
}

static bool send_packet(uint8_t op, uint8_t arg, uint16_t id)
{
    uint8_t packet[32];
    const unsigned int size = encode_packet(packet, op, arg, id);
    if ((size & 1u) != 0 || size > sizeof(packet)) return false;

    RADIO_PrepareTX();
    if (gCurrentFunction != FUNCTION_TRANSMIT) return false;
    SYSTEM_DelayMs(60);

    const uint16_t r3f = BK4819_ReadRegister(BK4819_REG_3F);
    const uint16_t r51 = BK4819_ReadRegister(BK4819_REG_51);
    const uint16_t r40 = BK4819_ReadRegister((BK4819_REGISTER_t)0x40);
    const uint16_t r2b = BK4819_ReadRegister(BK4819_REG_2B);

    BK4819_SetAF(BK4819_AF_MUTE);
    BK4819_WriteRegister(BK4819_REG_51, 0);
    BK4819_WriteRegister((BK4819_REGISTER_t)0x40, (r40 & 0xF000u) | 850u);
    BK4819_WriteRegister(BK4819_REG_2B, (1u << 2) | 1u);
    BK4819_WriteRegister(BK4819_REG_58, (1u << 13) | (7u << 10) | (1u << 1) | 1u);
    BK4819_WriteRegister(BK4819_REG_72, (uint16_t)((1200u * 1353245u + (1u << 16)) >> 17));
    BK4819_WriteRegister(BK4819_REG_70, (1u << 7) | 96u);

    uint16_t reg59 = (3u << 4) | (1u << 3);
    BK4819_WriteRegister(BK4819_REG_5D, ((size - 1u) << 8));
    BK4819_WriteRegister(BK4819_REG_5A, 0);
    BK4819_WriteRegister(BK4819_REG_5B, 0);
    BK4819_WriteRegister(BK4819_REG_5C, 0x5625);
    BK4819_WriteRegister(BK4819_REG_59, (1u << 15) | (1u << 14) | reg59);
    BK4819_WriteRegister(BK4819_REG_59, reg59);

    const uint16_t *words = (const uint16_t *)packet;
    for (unsigned int i = 0; i < size / 2; i++)
        BK4819_WriteRegister(BK4819_REG_5F, words[i]);

    BK4819_WriteRegister(BK4819_REG_3F, BK4819_REG_3F_FSK_TX_FINISHED);
    BK4819_WriteRegister(BK4819_REG_59, (1u << 11) | reg59);

    unsigned int timeout = 80;
    while (timeout--) {
        SYSTEM_DelayMs(4);
        if (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
            BK4819_WriteRegister(BK4819_REG_02, 0);
            if (BK4819_ReadRegister(BK4819_REG_02) & BK4819_REG_02_FSK_TX_FINISHED)
                break;
        }
    }

    BK4819_WriteRegister(BK4819_REG_59, reg59);
    BK4819_WriteRegister(BK4819_REG_70, 0);
    BK4819_WriteRegister(BK4819_REG_58, 0);
    BK4819_WriteRegister((BK4819_REGISTER_t)0x40, r40);
    BK4819_WriteRegister(BK4819_REG_2B, r2b);
    BK4819_WriteRegister(BK4819_REG_51, r51);
    BK4819_WriteRegister(BK4819_REG_3F, r3f);

    RADIO_SetupRegisters(true);
    return true;
}

bool MDC1200_send_call_alert(void)
{
    const bool ok = send_packet(0x35, 0x89, AIRCFG_GetMdcId());
    if (ok) {
        mdc1200_op = 0x35;
        mdc1200_arg = 0x89;
        mdc1200_unit_id = AIRCFG_GetMdcId();
        record_event(mdc1200_unit_id, mdc1200_op, mdc1200_arg);
    }
    return ok;
}

bool MDC1200_send_group_control(uint16_t group_id, bool open)
{
    if (group_id == 0 || group_id == 0xFFFFu) return false;
    const uint8_t arg = open ? 0xA1u : 0xA0u;
    const bool ok = send_packet(0x47u, arg, group_id);
    if (ok) {
        mdc1200_op = 0x47u;
        mdc1200_arg = arg;
        mdc1200_unit_id = group_id;
        record_event(group_id, 0x47u, arg);
    }
    return ok;
}


static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

bool MDC1200_handle_local_code(const char *code)
{
    if (!code) return false;
    if (strcmp(code, "101") == 0) {
        MDC1200_send_call_alert();
        return true;
    }
    if (strlen(code) == 7 && code[0] == '1' && code[1] == '0' && code[2] == '0') {
        uint16_t id = 0;
        for (unsigned int i = 3; i < 7; i++) {
            const int d = hex_digit(code[i]);
            if (d < 0) return false;
            id = (id << 4) | d;
        }
        AIRCFG_SetMdcId(id);
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
        return true;
    }
    return false;
}

void MDC1200_open_log(void)
{
    if (history_count == 0) return;
    log_open = true;
    history_selected = 0;
    mdc1200_rx_ready_tick_500ms = 20;
    gUpdateDisplay = true;
}

bool MDC1200_is_overlay_active(void)
{
    return edit_mode || (log_open && history_count > 0 && mdc1200_rx_ready_tick_500ms > 0);
}

static void begin_edit(void)
{
    MDC1200_Event *e = selected_event();
    if (!e) return;
    char found[11];
    memset(edit_name, ' ', 10);
    edit_name[10] = 0;
    if (contact_find(e->unit_id, found, 0)) {
        unsigned int n = strlen(found);
        if (n > 10) n = 10;
        memcpy(edit_name, found, n);
    }
    edit_pos = 0;
    edit_last_key = KEY_INVALID;
    edit_mode = true;
    mdc1200_rx_ready_tick_500ms = 60;
    gUpdateDisplay = true;
}

static void save_edit(void)
{
    MDC1200_Event *e = selected_event();
    if (!e) { edit_mode = false; return; }
    char name[11];
    memcpy(name, edit_name, 10);
    name[10] = 0;
    for (int i = 9; i >= 0 && name[i] == ' '; i--) name[i] = 0;
    if (name[0] == 0) strcpy(name, "UNIT");
    contact_save(e->unit_id, name);
    edit_mode = false;
    edit_last_key = KEY_INVALID;
    mdc1200_rx_ready_tick_500ms = 8;
    gUpdateDisplay = true;
    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
}

static char cycle_char(char c, int dir)
{
    static const char chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    int pos = 0;
    for (unsigned int i = 0; i < sizeof(chars) - 1; i++) if (chars[i] == c) { pos = i; break; }
    const int n = sizeof(chars) - 1;
    pos = (pos + dir + n) % n;
    return chars[pos];
}

static void keypad_letter(KEY_Code_t key)
{
    static const char *groups[10] = {
        " ", "1", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ"
    };
    const unsigned int k = key - KEY_0;
    const char *group = groups[k];
    if (edit_last_key == key) {
        char *p = strchr(group, edit_name[edit_pos]);
        if (p && p[1]) edit_name[edit_pos] = p[1];
        else edit_name[edit_pos] = group[0];
    } else {
        edit_name[edit_pos] = group[0];
        edit_last_key = key;
    }
}

bool MDC1200_handle_key(KEY_Code_t key, bool pressed, bool held)
{
    if (!MDC1200_is_overlay_active()) return false;

    if (!pressed) return true;

    if (edit_mode) {
        if (key == KEY_MENU) {
            if (held) { save_edit(); return true; }
            if (edit_pos < 9) edit_pos++;
            else save_edit();
            edit_last_key = KEY_INVALID;
        } else if (key == KEY_EXIT) {
            edit_mode = false;
            edit_last_key = KEY_INVALID;
            mdc1200_rx_ready_tick_500ms = 12;
            gUpdateDisplay = true;
        } else if (key == KEY_UP) {
            edit_name[edit_pos] = cycle_char(edit_name[edit_pos], 1);
            edit_last_key = KEY_INVALID;
            gUpdateDisplay = true;
        } else if (key == KEY_DOWN) {
            edit_name[edit_pos] = cycle_char(edit_name[edit_pos], -1);
            edit_last_key = KEY_INVALID;
            gUpdateDisplay = true;
        } else if (key <= KEY_9 && !held) {
            keypad_letter(key);
            gUpdateDisplay = true;
        } else if (key == KEY_STAR && !held) {
            edit_name[edit_pos] = '-';
            edit_last_key = KEY_INVALID;
            gUpdateDisplay = true;
        } else if (key == KEY_F && !held) {
            edit_name[edit_pos] = ' ';
            edit_last_key = KEY_INVALID;
            gUpdateDisplay = true;
        }
        return true;
    }

    if (held) return true;

    if (key == KEY_MENU) {
        begin_edit();
    } else if (key == KEY_UP) {
        if (history_selected + 1 < history_count) history_selected++;
        mdc1200_rx_ready_tick_500ms = 20;
        gUpdateDisplay = true;
    } else if (key == KEY_DOWN) {
        if (history_selected > 0) history_selected--;
        mdc1200_rx_ready_tick_500ms = 20;
        gUpdateDisplay = true;
    } else if (key == KEY_EXIT) {
        log_open = false;
        mdc1200_rx_ready_tick_500ms = 0;
        gUpdateDisplay = true;
    }
    return true;
}

bool MDC1200_render(void)
{
    if (!MDC1200_is_overlay_active()) return false;

    MDC1200_Event *e = selected_event();
    if (!e) return false;

    UI_DisplayClear();
    char line[24];

    if (edit_mode) {
        sprintf(line, "MDC %04X NAME", e->unit_id);
        UI_PrintStringSmallBold(line, 2, 0, 0);
        UI_PrintString(edit_name, 2, 0, 2, 8);
        memset(line, ' ', 10);
        line[10] = 0;
        if (edit_pos < 10) line[edit_pos] = '^';
        UI_PrintStringSmallNormal(line, 2, 0, 4);
        UI_PrintStringSmallNormal("2-9 letters UP/DN", 2, 0, 5);
        UI_PrintStringSmallNormal("MENU next HOLD=SAVE", 2, 0, 6);
        ST7565_BlitFullScreen();
        return true;
    }

    const char *kind = MDC1200_event_name(e->op, e->arg);
    UI_PrintString(kind, 2, 0, 0, 8);

    char name[11];
    if (contact_find(e->unit_id, name, 0) && name[0])
        sprintf(line, "%s %04X", name, e->unit_id);
    else
        sprintf(line, "MDC %04X", e->unit_id);
    UI_PrintString(line, 2, 0, 2, 8);

    if (e->channel <= MR_CHANNEL_LAST)
        sprintf(line, "CH%03u  %3u.%05u", e->channel + 1u, e->frequency / 100000u, e->frequency % 100000u);
    else
        sprintf(line, "VFO    %3u.%05u", e->frequency / 100000u, e->frequency % 100000u);
    UI_PrintStringSmallNormal(line, 2, 0, 4);

    sprintf(line, "LOG %u/%u OP%02X A%02X", history_selected + 1u, history_count, e->op, e->arg);
    UI_PrintStringSmallNormal(line, 2, 0, 5);

    if (contact_find(e->unit_id, name, 0))
        UI_PrintStringSmallNormal("UP/DN LOG MENU=EDIT", 2, 0, 6);
    else
        UI_PrintStringSmallNormal("UP/DN LOG MENU=SAVE", 2, 0, 6);

    ST7565_BlitFullScreen();
    return true;
}

void MDC1200_time_slice_500ms(void)
{
    tick500++;
    if (!edit_mode && mdc1200_rx_ready_tick_500ms > 0) {
        if (--mdc1200_rx_ready_tick_500ms == 0) {
            log_open = false;
            gUpdateDisplay = true;
        }
    }
}

void MDC1200_init(void)
{
    memcpy(sync_xor, mdc_sync, sizeof(sync_xor));
    xor_modulation(sync_xor, sizeof(sync_xor));
    memset(history, 0, sizeof(history));
    history_count = 0;
    history_head = 0;
    history_selected = 0;
    rx_index = 0;
    log_open = false;
    edit_mode = false;
    tick500 = 0;
    last_id = 0;
    last_op = 0;
    last_arg = 0;
    last_tick = 0;
}
