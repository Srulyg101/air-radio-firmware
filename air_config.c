#include <string.h>
#include "air_config.h"
#include "air_nv.h"

#define AIRCFG_ADDR 240u
#define AIRCFG_MAGIC 0xA7u
#define AIRCFG_VERSION 1u

typedef struct {
    uint8_t magic;
    uint8_t version;
    uint16_t mdc_id;
    uint8_t mode;
    uint8_t shabbos;
    uint8_t reserved[10];
} __attribute__((packed)) AirConfigRecord;

static AirConfigRecord cfg;

static void save(void)
{
    AIRNV_Write(AIRCFG_ADDR, &cfg, sizeof(cfg));
}

void AIRCFG_Init(void)
{
    AIRNV_Read(AIRCFG_ADDR, &cfg, sizeof(cfg));
    if (cfg.magic != AIRCFG_MAGIC || cfg.version != AIRCFG_VERSION ||
        cfg.mdc_id == 0 || cfg.mdc_id == 0xFFFFu || cfg.mode > 2 || cfg.shabbos > 1) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = AIRCFG_MAGIC;
        cfg.version = AIRCFG_VERSION;
        cfg.mdc_id = 0x1201u;
        cfg.mode = 0;
        cfg.shabbos = 0;
        save();
    }
}

uint16_t AIRCFG_GetMdcId(void) { return cfg.mdc_id; }
void AIRCFG_SetMdcId(uint16_t id)
{
    if (id == 0 || id == 0xFFFFu) return;
    cfg.mdc_id = id;
    save();
}
uint8_t AIRCFG_GetMode(void) { return cfg.mode; }
void AIRCFG_SetMode(uint8_t mode)
{
    if (mode > 2) mode = 0;
    cfg.mode = mode;
    save();
}
bool AIRCFG_GetShabbos(void) { return cfg.shabbos != 0; }
void AIRCFG_SetShabbos(bool enabled)
{
    cfg.shabbos = enabled ? 1 : 0;
    save();
}
