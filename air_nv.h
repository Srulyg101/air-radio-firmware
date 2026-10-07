#ifndef AIR_NV_H
#define AIR_NV_H

#include <stdint.h>

#define AIRNV_SIZE 384u

void AIRNV_Read(uint16_t offset, void *buffer, uint16_t size);
void AIRNV_Write(uint16_t offset, const void *buffer, uint16_t size);

#endif
