// app_config.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_MAGIC  0xC0A5FEEDu
#define CFG_VER    0x0003u   // was 0x0002u :contentReference[oaicite:1]{index=1}

// ===== Battery type =====
typedef enum {
    BATTERY_12V = 12,
    BATTERY_24V = 24,
    BATTERY_36V = 36,
    BATTERY_48V = 48,
} battery_type_t;

// ===== PSU battery/profile config =====
typedef struct
{
    // Battery configuration
    battery_type_t battery_type;     // 12V/24V/36V/48V
    float battery_capacity_ah;       // 10.0, 20.0, 50.0, 100.0

    // Voltage thresholds (auto-loaded from profile)
    float voltage_cutoff_low;        // Emergency cutoff
    float voltage_warning;           // Warning threshold (≈30% SOC)
    float voltage_critical;          // Critical threshold (≈20% SOC)

    // Trap enable flags
    uint8_t warning_60min_enabled;   // 1=enabled
    uint8_t warning_30min_enabled;
    uint8_t warning_15min_enabled;
    uint8_t warning_5min_enabled;
    uint8_t warning_soc30_enabled;
    uint8_t warning_soc20_enabled;
    uint8_t warning_soc10_enabled;

    // Site identification
    char site_identifier[32];        // "Building-5-IDF2"
    char snmp_trap_dest[64];         // NOC IP / hostname
    uint16_t snmp_trap_port;         // 162

    // Advanced settings
    float current_sensor_offset;     // Calibration offset
    float voltage_sensor_scale;      // Calibration scale
    uint8_t enable_auto_shutdown;    // Auto shutdown at emergency voltage
} psu_config_t;

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

const app_config_t* app_cfg_get(void);
bool app_cfg_set(const app_config_t* cfg);

// Helpers cho battery/profile
void psu_profile_apply_defaults(psu_config_t *p);
const psu_config_t* app_cfg_psu_get(void);
bool app_cfg_psu_set(const psu_config_t *in);

#ifdef __cplusplus
}
#endif
