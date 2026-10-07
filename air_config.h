#ifndef AIR_CONFIG_H
#define AIR_CONFIG_H
#include <stdbool.h>
#include <stdint.h>

void AIRCFG_Init(void);
uint16_t AIRCFG_GetMdcId(void);
void AIRCFG_SetMdcId(uint16_t id);
uint8_t AIRCFG_GetMode(void);
void AIRCFG_SetMode(uint8_t mode);
bool AIRCFG_GetShabbos(void);
void AIRCFG_SetShabbos(bool enabled);

#endif
