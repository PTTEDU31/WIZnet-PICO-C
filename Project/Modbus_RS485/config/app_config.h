#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_MAGIC  0xC0A5FEEDu
#define CFG_VER    0x0002u

typedef struct {
    // ===== Network =====
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t gw[4];
    uint8_t sn[4];
    uint8_t dns[4];
    uint8_t dhcp_enable;

    // ===== SNMP =====
    uint8_t snmp_manager_ip[4];
    uint8_t trap_enable_mask; // bit0: powerFail, bit1: overTemp, bit2: overCurr
    uint8_t snmp_enable;

    // ===== PSU / Modbus =====
    uint8_t psu_slave_addr;
    float psu_in_undervolt_V;
    float psu_out_overvolt_V;
    float psu_overcurrent_A;
    float psu_overtemp_C;
    float hysteresis_pct;
    uint32_t debounce_ms;

    // ===== System Info =====
    char device_name[32];
    char uuid[40];
    int8_t timezone;     // +7 for Vietnam, etc.
    uint8_t web_enable;
    uint8_t sntp_enable;
    uint8_t mqtt_enable;
    uint8_t ota_enable;

    // reserved for future
    uint8_t reserved[32];
} app_config_t;

// API
bool app_cfg_init(void);
bool app_cfg_save(void);
void app_cfg_reset_default(void);

const app_config_t* app_cfg_get(void);
bool app_cfg_set(const app_config_t* cfg);

#ifdef __cplusplus
}
#endif
