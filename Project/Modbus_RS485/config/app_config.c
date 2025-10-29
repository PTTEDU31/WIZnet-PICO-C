#include "app_config.h"
#include <string.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "stdio.h"

#define CFG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct
{
    uint32_t magic;
    uint16_t ver;
    uint16_t len;
    app_config_t cfg;
    uint32_t crc32;
} cfg_image_t;

static app_config_t g_cfg;

// ===== CRC =====
static uint32_t  __not_in_flash_func (crc32_update)(uint32_t c, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    c = ~c;
    for (size_t i = 0; i < len; i++)
    {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (-(int)(c & 1)));
    }
    return ~c;
}

static bool __not_in_flash_func(cfg_is_valid)(const cfg_image_t *img)
{
    if (img->magic != CFG_MAGIC || img->ver != CFG_VER)
        return false;
    if (img->len != sizeof(app_config_t))
        return false;
    uint32_t crc = crc32_update(0, &img->cfg, sizeof(app_config_t));
    return (crc == img->crc32);
}

// ===== Default values =====
void app_cfg_reset_default(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.mac[0] = 0x00;
    g_cfg.mac[1] = 0x08;
    g_cfg.mac[2] = 0xDC;
    g_cfg.mac[3] = 0x12;
    g_cfg.mac[4] = 0x34;
    g_cfg.mac[5] = 0x56;
    g_cfg.ip[0] = 192;
    g_cfg.ip[1] = 168;
    g_cfg.ip[2] = 137;
    g_cfg.ip[3] = 123;
    g_cfg.sn[0] = 255;
    g_cfg.sn[1] = 255;
    g_cfg.sn[2] = 255;
    g_cfg.sn[3] = 0;
    g_cfg.gw[0] = 192;
    g_cfg.gw[1] = 168;
    g_cfg.gw[2] = 137;
    g_cfg.gw[3] = 1;
    g_cfg.dns[0] = 8;
    g_cfg.dns[1] = 8;
    g_cfg.dns[2] = 8;
    g_cfg.dns[3] = 8;
    g_cfg.dhcp_enable = 0;

    g_cfg.snmp_manager_ip[0] = 192;
    g_cfg.snmp_manager_ip[1] = 168;
    g_cfg.snmp_manager_ip[2] = 137;
    g_cfg.snmp_manager_ip[3] = 1;
    g_cfg.trap_enable_mask = 0x07;
    g_cfg.snmp_enable = 1;

    g_cfg.psu_slave_addr = 131;
    g_cfg.psu_in_undervolt_V = 180.0f;
    g_cfg.psu_out_overvolt_V = 250.0f;
    g_cfg.psu_overcurrent_A = 2.5f;
    g_cfg.psu_overtemp_C = 70.0f;
    g_cfg.hysteresis_pct = 2.0f;
    g_cfg.debounce_ms = 1000;

    strcpy(g_cfg.device_name, "PSU-Monitor");
    strcpy(g_cfg.uuid, "00000000-0000-0000-0000-000000000000");
    g_cfg.timezone = +7;
    g_cfg.web_enable = 1;
    g_cfg.sntp_enable = 1;
    g_cfg.mqtt_enable = 0;
    g_cfg.ota_enable = 0;
}
// This function will be called when it's safe to call flash_range_erase
static void __not_in_flash_func(call_flash_range_erase)(void *param) {
    uint32_t offset = (uint32_t)param;
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
}

// This function will be called when it's safe to call flash_range_program
static void __not_in_flash_func(call_flash_range_program)(void *param) {
    uint32_t offset = ((uintptr_t*)param)[0];
    const uint8_t *data = (const uint8_t *)((uintptr_t*)param)[1];
    flash_range_program(offset, data, FLASH_PAGE_SIZE);
}

// ===== Flash I/O =====
bool __not_in_flash_func(app_cfg_save)(void)
{
    cfg_image_t img;
    img.magic = CFG_MAGIC;
    img.ver   = CFG_VER;
    img.len   = sizeof(app_config_t);
    img.cfg   = g_cfg;
    img.crc32 = crc32_update(0, &img.cfg, sizeof(app_config_t));

    printf("\n[FLASH] Saving config via flash_safe_execute()...\n");

    int rc = flash_safe_execute(call_flash_range_erase, (void *)CFG_FLASH_OFFSET, UINT32_MAX);
    if (rc != PICO_OK) {
        printf("[FLASH] ❌ Erase failed! rc=%d\n", rc);
        return false;
    }
    uintptr_t params[] = { CFG_FLASH_OFFSET, (uintptr_t)&img };
    rc = flash_safe_execute(call_flash_range_program, params, UINT32_MAX);
    if (rc != PICO_OK) {
        printf("[FLASH] ❌ Program failed! rc=%d\n", rc);
        return false;
    }

    const cfg_image_t *chk = (const cfg_image_t *)(XIP_BASE + CFG_FLASH_OFFSET);
    bool ok = cfg_is_valid(chk);
    printf("[FLASH] Validation: %s (CRC=0x%08lx)\n", ok ? "✅ OK" : "❌ FAIL", chk->crc32);
    return ok;
}

bool app_cfg_init(void)
{
    const cfg_image_t *img = (const cfg_image_t *)(XIP_BASE + CFG_FLASH_OFFSET);
    if (cfg_is_valid(img))
    {
        g_cfg = img->cfg;
        return true;
    }
    app_cfg_reset_default();
    return app_cfg_save();
}

const app_config_t *app_cfg_get(void) { return &g_cfg; }

bool app_cfg_set(const app_config_t *in)
{
    if (!in)
        return false;
    g_cfg = *in;
    return true;
}
