#ifndef AIR_MODES_H
#define AIR_MODES_H
#include <stdbool.h>
#include "driver/keyboard.h"

enum {
    AIR_MODE_NORMAL = 0,
    AIR_MODE_21 = 1,
    AIR_MODE_23 = 2
};

void AIRMODES_Init(void);
bool AIRMODES_HandleLocalCode(const char *code);
void AIRMODES_ObserveKey(KEY_Code_t key, bool pressed, bool held);
void AIRMODES_BeforePtt(void);
void AIRMODES_AfterPtt(void);
bool AIRMODES_IsShabbos(void);
unsigned int AIRMODES_GetMode(void);

#endif
