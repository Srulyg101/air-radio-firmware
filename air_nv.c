#include <string.h>
#include "air_nv.h"
#include "driver/eeprom.h"

#define CHANNEL_NAME_BASE 0x0F50u
#define CHANNEL_NAME_STRIDE 16u
#define CHANNEL_NAME_TAIL_OFFSET 8u
#define AIRNV_BYTES_PER_CHANNEL 6u
#define AIRNV_CHANNEL_COUNT (AIRNV_SIZE / AIRNV_BYTES_PER_CHANNEL)

void AIRNV_Read(uint16_t offset, void *buffer, uint16_t size)
{
    uint8_t *out = (uint8_t *)buffer;
    if (buffer == 0 || offset >= AIRNV_SIZE)
        return;
    if ((uint32_t)offset + size > AIRNV_SIZE)
        size = AIRNV_SIZE - offset;

    while (size > 0) {
        const uint16_t channel = offset / AIRNV_BYTES_PER_CHANNEL;
        const uint8_t in_slot = offset % AIRNV_BYTES_PER_CHANNEL;
        uint8_t block[8];

        if (channel >= AIRNV_CHANNEL_COUNT)
            return;

        EEPROM_ReadBuffer(CHANNEL_NAME_BASE + channel * CHANNEL_NAME_STRIDE + CHANNEL_NAME_TAIL_OFFSET,
                          block, sizeof(block));

        uint8_t chunk = AIRNV_BYTES_PER_CHANNEL - in_slot;
        if (chunk > size)
            chunk = size;

        memcpy(out, &block[2 + in_slot], chunk);
        out += chunk;
        offset += chunk;
        size -= chunk;
    }
}

void AIRNV_Write(uint16_t offset, const void *buffer, uint16_t size)
{
    const uint8_t *in = (const uint8_t *)buffer;
    if (buffer == 0 || offset >= AIRNV_SIZE)
        return;
    if ((uint32_t)offset + size > AIRNV_SIZE)
        size = AIRNV_SIZE - offset;

    while (size > 0) {
        const uint16_t channel = offset / AIRNV_BYTES_PER_CHANNEL;
        const uint8_t in_slot = offset % AIRNV_BYTES_PER_CHANNEL;
        const uint16_t address = CHANNEL_NAME_BASE + channel * CHANNEL_NAME_STRIDE + CHANNEL_NAME_TAIL_OFFSET;
        uint8_t block[8];

        if (channel >= AIRNV_CHANNEL_COUNT)
            return;

        EEPROM_ReadBuffer(address, block, sizeof(block));

        uint8_t chunk = AIRNV_BYTES_PER_CHANNEL - in_slot;
        if (chunk > size)
            chunk = size;

        memcpy(&block[2 + in_slot], in, chunk);
        EEPROM_WriteBuffer(address, block);

        in += chunk;
        offset += chunk;
        size -= chunk;
    }
}
