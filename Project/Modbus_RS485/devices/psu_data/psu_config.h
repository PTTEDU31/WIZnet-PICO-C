#ifndef PSU_CONFIG_H
#define PSU_CONFIG_H

#include <stdint.h>

// =============================================================
// Battery Voltage Configuration Types
// =============================================================
typedef enum {
    BATT_TYPE_12V = 0,  // 4S LiFePO4 (12.8V nominal)
    BATT_TYPE_24V = 1,  // 8S LiFePO4 (25.6V nominal)
    BATT_TYPE_36V = 2,  // 12S LiFePO4 (38.4V nominal)
    BATT_TYPE_48V = 3,  // 16S LiFePO4 (51.2V nominal)
    BATT_TYPE_COUNT
} battery_type_t;

// =============================================================
// Voltage Profile for each battery type (SPEC 2.1, 2.2)
// =============================================================
typedef struct {
    battery_type_t type;
    uint8_t cells_in_series;      // 4S, 8S, 12S, 16S
    float nominal_voltage;         // 12.8V, 25.6V, 38.4V, 51.2V
    float charge_voltage;          // 14.6V, 29.2V, 43.8V, 58.4V
    float discharge_cutoff;        // 12.0V, 24.0V, 36.0V, 48.0V
    float emergency_cutoff;        // 11.2V, 22.4V, 33.6V, 44.8V
    
    // Voltage-to-SOC mapping (11 points)
    struct {
        float voltage;
        float soc;
    } soc_table[11];
} voltage_profile_t;

// =============================================================
// Runtime configuration structure (SPEC 7.1)
// =============================================================
typedef struct
{
    // Battery configuration
    battery_type_t battery_type;     // 12V/24V/36V/48V
    float battery_capacity_ah;       // 10.0, 20.0, 50.0, 100.0
    
    // Voltage thresholds (auto-loaded from profile)
    float voltage_cutoff_low;        // Emergency cutoff
    float voltage_warning;           // Warning threshold (30% SOC)
    float voltage_critical;          // Critical threshold (20% SOC)
    
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
    uint8_t snmp_trap_dest[4];         // NOC IP address
    uint16_t snmp_trap_port;         // 162
    
    // Advanced settings
    float current_sensor_offset;     // Calibration offset
    float voltage_sensor_scale;      // Calibration scale
    uint8_t enable_auto_shutdown;    // Auto shutdown at emergency voltage
} psu_config_t;

// =============================================================
// Global configuration instance
// =============================================================
extern psu_config_t g_psu_config;
extern const voltage_profile_t g_voltage_profiles[BATT_TYPE_COUNT];

// =============================================================
// Configuration API
// =============================================================

/**
 * Initialize configuration with defaults
 */
void psu_config_init(void);

/**
 * Set battery type (12V/24V/36V/48V)
 * This automatically loads voltage profile
 */
void psu_config_set_battery_type(battery_type_t type);

/**
 * Set battery capacity in Ah
 */
void psu_config_set_capacity(float ah);

/**
 * Set site identifier string
 */
void psu_config_set_site_id(const char *id);

/**
 * Set SNMP trap destination IP
 */
void psu_config_set_trap_dest(const char *ip);

/**
 * Enable/disable specific warning traps
 */
void psu_config_enable_trap(uint8_t trap_id, uint8_t enable);

/**
 * Get current voltage profile
 */
const voltage_profile_t* psu_config_get_voltage_profile(void);

/**
 * Convert voltage to SOC% based on current battery type
 */
float psu_config_voltage_to_soc(float voltage);

/**
 * Get voltage threshold for specific SOC%
 */
float psu_config_soc_to_voltage(float soc_percent);

/**
 * Save configuration to persistent storage (EEPROM/Flash)
 */
void psu_config_save(void);

/**
 * Load configuration from persistent storage
 */
void psu_config_load(void);

/**
 * Print current configuration (debug)
 */
void psu_config_print(void);

// =============================================================
// Trap ID definitions for enable/disable
// =============================================================
#define TRAP_ID_RUNTIME_60MIN   0
#define TRAP_ID_RUNTIME_30MIN   1
#define TRAP_ID_RUNTIME_15MIN   2
#define TRAP_ID_RUNTIME_5MIN    3
#define TRAP_ID_SOC_30PCT       4
#define TRAP_ID_SOC_20PCT       5
#define TRAP_ID_SOC_10PCT       6

#endif // PSU_CONFIG_H