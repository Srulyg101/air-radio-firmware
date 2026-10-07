#ifndef AIR_MDC1200_H
#define AIR_MDC1200_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/keyboard.h"

#define MDC1200_FEC_K 7
#define MDC1200_HISTORY_SIZE 16
#define MDC1200_MAX_CONTACTS 15

typedef struct {
    uint16_t unit_id;
    uint8_t op;
    uint8_t arg;
    uint8_t channel;
    uint32_t frequency;
    uint16_t tick;
} MDC1200_Event;

void MDC1200_init(void);
void MDC1200_enable_rx(bool enable);
void MDC1200_process_rx(uint16_t interrupt_bits);
void MDC1200_time_slice_500ms(void);

bool MDC1200_render(void);
bool MDC1200_handle_key(KEY_Code_t key, bool pressed, bool held);
void MDC1200_open_log(void);

bool MDC1200_is_overlay_active(void);
bool MDC1200_send_call_alert(void);
bool MDC1200_handle_local_code(const char *code);

const char *MDC1200_event_name(uint8_t op, uint8_t arg);

#endif
