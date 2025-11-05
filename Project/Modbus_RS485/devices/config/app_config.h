// app_config.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "psu_config.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CFG_MAGIC  0xC0A5FEEDu
#define CFG_VER    0x0003u   // was 0x0002u :contentReference[oaicite:1]{index=1}


typedef struct {
    // ===== Network =====
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t gw[4];
    uint8_t sn[4];
    uint8_t dns[4];
    uint8_t dhcp_enable;

    // ===== SNMP (giữ cấu hình cũ để tương thích) =====
    uint8_t snmp_manager_ip[4];
    uint8_t trap_enable_mask; // bit0: powerFail, bit1: overTemp, bit2: overCurr
    uint8_t snmp_enable;

    // ===== PSU / Modbus (giữ cấu hình cũ) =====
    uint8_t  psu_slave_addr;
    float    psu_in_undervolt_V;
    float    psu_out_overvolt_V;
    float    psu_overcurrent_A;
    float    psu_overtemp_C;
    float    hysteresis_pct;
    uint32_t debounce_ms;

    // ===== Battery/Profile (mới) =====
    psu_config_t psu;

    // ===== System Info =====
    char   device_name[32];
    char   uuid[40];
    int8_t timezone;     // +7 for Vietnam, etc.
    uint8_t web_enable;
    uint8_t sntp_enable;
    uint8_t mqtt_enable;
    uint8_t ota_enable;

    // reserved (giảm còn 8 để giữ tổng size gọn; zero-filled)
    uint8_t reserved[8];
} app_config_t;

// ===== API =====
bool app_cfg_init(void);
bool app_cfg_save(void);
void app_cfg_reset_default(void);


void appcfg_defaults(app_config_t* c);
bool appcfg_validate(const app_config_t* c, char* why, size_t why_len);
bool appcfg_to_json(const app_config_t* c, char** out_json);        // cJSON_free(out_json)
bool appcfg_apply_json(app_config_t* c, const char* body, size_t len, char* err, size_t errlen);

const app_config_t* app_cfg_get(void);
bool app_cfg_set(const app_config_t* cfg);

// Helpers cho battery/profile
void psu_profile_apply_defaults(psu_config_t *p);
const psu_config_t* app_cfg_psu_get(void);
bool app_cfg_psu_set(const psu_config_t *in);

#ifdef __cplusplus
}
#endif
