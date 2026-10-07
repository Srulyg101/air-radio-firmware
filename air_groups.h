#ifndef AIR_GROUPS_H
#define AIR_GROUPS_H
#include <stdbool.h>
#include <stdint.h>

#define AIRGROUP_MAX_PROFILES 16

void AIRGROUP_Init(void);
bool AIRGROUP_HandleLocalCode(const char *code);
bool AIRGROUP_ShouldUnmute(uint8_t channel);
void AIRGROUP_OnMdcPacket(uint8_t op, uint8_t arg, uint16_t id);
bool AIRGROUP_ChannelHasGroups(uint8_t channel);
uint16_t AIRGROUP_FirstGroupForChannel(uint8_t channel);

#endif
