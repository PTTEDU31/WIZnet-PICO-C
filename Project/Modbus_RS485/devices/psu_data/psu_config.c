#include "psu_config.h"
#include <string.h>
#include <stdio.h>
#include "app_config.h"

// =============================================================
// Voltage Profiles Database (Based on LiFePO4 chemistry)
// =============================================================
const voltage_profile_t g_voltage_profiles[BATT_TYPE_COUNT] = {
    // 12V System (4S LiFePO4)
    {
        .type = BATT_TYPE_12V,
        .cells_in_series = 4,
        .nominal_voltage = 12.8f,
        .charge_voltage = 14.6f,
        .discharge_cutoff = 12.0f,
        .emergency_cutoff = 11.2f,
        .soc_table = {
            {14.6f, 100.0f},  // Fully charged
            {14.0f,  95.0f},
            {13.6f,  90.0f},
            {13.4f,  80.0f},
            {13.25f, 70.0f},
            {13.2f,  60.0f},
            {13.0f,  50.0f},
            {12.8f,  40.0f},
            {12.6f,  30.0f},  // Warning threshold
            {12.4f,  20.0f},  // Critical threshold
            {12.0f,  10.0f}   // Emergency shutdown
        }
    },
    
    // 24V System (8S LiFePO4)
    {
        .type = BATT_TYPE_24V,
        .cells_in_series = 8,
        .nominal_voltage = 25.6f,
        .charge_voltage = 29.2f,
        .discharge_cutoff = 24.0f,
        .emergency_cutoff = 22.4f,
        .soc_table = {
            {29.2f, 100.0f},  // Fully charged
            {28.0f,  95.0f},
            {27.2f,  90.0f},
            {26.8f,  80.0f},
            {26.5f,  70.0f},
            {26.4f,  60.0f},
            {26.0f,  50.0f},
            {25.6f,  40.0f},
            {25.2f,  30.0f},  // Warning threshold
            {24.8f,  20.0f},  // Critical threshold
            {24.0f,  10.0f}   // Emergency shutdown
        }
    },
    
    // 36V System (12S LiFePO4)
    {
        .type = BATT_TYPE_36V,
        .cells_in_series = 12,
        .nominal_voltage = 38.4f,
        .charge_voltage = 43.8f,
        .discharge_cutoff = 36.0f,
        .emergency_cutoff = 33.6f,
        .soc_table = {
            {43.8f, 100.0f},  // Fully charged
            {42.0f,  95.0f},
            {40.8f,  90.0f},
            {40.2f,  80.0f},
            {39.75f, 70.0f},
            {39.6f,  60.0f},
            {39.0f,  50.0f},
            {38.4f,  40.0f},
            {37.8f,  30.0f},  // Warning threshold
            {37.2f,  20.0f},  // Critical threshold
            {36.0f,  10.0f}   // Emergency shutdown
        }
    },
    
    // 48V System (16S LiFePO4) - SPEC 2.2
    {
        .type = BATT_TYPE_48V,
        .cells_in_series = 16,
        .nominal_voltage = 51.2f,
        .charge_voltage = 58.4f,
        .discharge_cutoff = 48.0f,
        .emergency_cutoff = 44.8f,
        .soc_table = {
            {58.4f, 100.0f},  // Fully charged
            {56.0f,  95.0f},
            {54.4f,  90.0f},
            {53.6f,  80.0f},
            {53.0f,  70.0f},
            {52.8f,  60.0f},
            {52.0f,  50.0f},
            {51.2f,  40.0f},
            {50.4f,  30.0f},  // Warning threshold
            {49.6f,  20.0f},  // Critical threshold
            {48.0f,  10.0f}   // Emergency shutdown
        }
    }
};

// =============================================================
// Global configuration instance (default: 48V, 10Ah)
// =============================================================
psu_config_t g_psu_config = {0};

// =============================================================
// Internal helper: Linear interpolation
// =============================================================
static float lerp(float x1, float y1, float x2, float y2, float x)
{
    if (x2 == x1) return y1;
    return y1 + (y2 - y1) * (x - x1) / (x2 - x1);
}

// =============================================================
// Configuration API Implementation
// =============================================================

void psu_config_init(void)
{
    // Try to load from persistent storage
    // If load fails, keep default values
    // psu_config_load();
    
    // Ensure voltage thresholds match battery type
    g_psu_config = app_cfg_get()->psu;
    psu_config_set_battery_type(g_psu_config.battery_type);
}

void psu_config_set_battery_type(battery_type_t type)
{
    if (type >= BATT_TYPE_COUNT) return;
    
    g_psu_config.battery_type = type;
    
    // Auto-load voltage thresholds from profile
    const voltage_profile_t *profile = &g_voltage_profiles[type];
    g_psu_config.voltage_cutoff_low = profile->discharge_cutoff;
    
    // Set warning/critical from SOC table
    for (int i = 0; i < 11; i++) {
        if (profile->soc_table[i].soc == 30.0f) {
            g_psu_config.voltage_warning = profile->soc_table[i].voltage;
        }
        if (profile->soc_table[i].soc == 20.0f) {
            g_psu_config.voltage_critical = profile->soc_table[i].voltage;
        }
    }
}

void psu_config_set_capacity(float ah)
{
    if (ah >= 1.0f && ah <= 500.0f) {
        g_psu_config.battery_capacity_ah = ah;
    }
}

void psu_config_set_site_id(const char *id)
{
    if (id && strlen(id) < sizeof(g_psu_config.site_identifier)) {
        strncpy(g_psu_config.site_identifier, id, 
                sizeof(g_psu_config.site_identifier) - 1);
        g_psu_config.site_identifier[sizeof(g_psu_config.site_identifier) - 1] = '\0';
    }
}

void psu_config_set_trap_dest(const char *ip)
{
    if (ip && strlen(ip) < sizeof(g_psu_config.snmp_trap_dest)) {
        strncpy(g_psu_config.snmp_trap_dest, ip,
                sizeof(g_psu_config.snmp_trap_dest) - 1);
        g_psu_config.snmp_trap_dest[sizeof(g_psu_config.snmp_trap_dest) - 1] = '\0';
    }
}

void psu_config_enable_trap(uint8_t trap_id, uint8_t enable)
{
    switch (trap_id) {
        case TRAP_ID_RUNTIME_60MIN: g_psu_config.warning_60min_enabled = enable; break;
        case TRAP_ID_RUNTIME_30MIN: g_psu_config.warning_30min_enabled = enable; break;
        case TRAP_ID_RUNTIME_15MIN: g_psu_config.warning_15min_enabled = enable; break;
        case TRAP_ID_RUNTIME_5MIN:  g_psu_config.warning_5min_enabled = enable; break;
        case TRAP_ID_SOC_30PCT:     g_psu_config.warning_soc30_enabled = enable; break;
        case TRAP_ID_SOC_20PCT:     g_psu_config.warning_soc20_enabled = enable; break;
        case TRAP_ID_SOC_10PCT:     g_psu_config.warning_soc10_enabled = enable; break;
    }
}

const voltage_profile_t* psu_config_get_voltage_profile(void)
{
    return &g_voltage_profiles[g_psu_config.battery_type];
}

float psu_config_voltage_to_soc(float voltage)
{
    const voltage_profile_t *profile = psu_config_get_voltage_profile();
    
    // Apply voltage sensor calibration
    voltage *= g_psu_config.voltage_sensor_scale;
    
    // Handle out-of-range cases
    if (voltage >= profile->soc_table[0].voltage) {
        return 100.0f;
    }
    if (voltage <= profile->soc_table[10].voltage) {
        return 5.0f;
    }
    
    // Linear interpolation between table points
    for (int i = 0; i < 10; i++) {
        float v_high = profile->soc_table[i].voltage;
        float v_low = profile->soc_table[i + 1].voltage;
        
        if (voltage <= v_high && voltage >= v_low) {
            float soc_high = profile->soc_table[i].soc;
            float soc_low = profile->soc_table[i + 1].soc;
            return lerp(v_low, soc_low, v_high, soc_high, voltage);
        }
    }
    
    return 50.0f; // Fallback
}

float psu_config_soc_to_voltage(float soc_percent)
{
    const voltage_profile_t *profile = psu_config_get_voltage_profile();
    
    if (soc_percent >= 100.0f) return profile->soc_table[0].voltage;
    if (soc_percent <= 5.0f) return profile->soc_table[10].voltage;
    
    // Reverse interpolation
    for (int i = 0; i < 10; i++) {
        float soc_high = profile->soc_table[i].soc;
        float soc_low = profile->soc_table[i + 1].soc;
        
        if (soc_percent <= soc_high && soc_percent >= soc_low) {
            float v_high = profile->soc_table[i].voltage;
            float v_low = profile->soc_table[i + 1].voltage;
            return lerp(soc_low, v_low, soc_high, v_high, soc_percent);
        }
    }
    
    return profile->nominal_voltage; // Fallback
}

void psu_config_save(void)
{
    // TODO: Save to EEPROM/Flash
    // Example for RP2040:
    // flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    // flash_range_program(CONFIG_FLASH_OFFSET, (uint8_t*)&g_psu_config, sizeof(psu_config_t));
    
    printf("[CONFIG] Configuration saved (not implemented)\n");
}

void psu_config_load(void)
{
    // TODO: Load from EEPROM/Flash
    // Example for RP2040:
    // const uint8_t *flash_addr = (const uint8_t *)(XIP_BASE + CONFIG_FLASH_OFFSET);
    // memcpy(&g_psu_config, flash_addr, sizeof(psu_config_t));
    
    printf("[CONFIG] Configuration loaded (not implemented)\n");
}

void psu_config_print(void)
{
    const voltage_profile_t *profile = psu_config_get_voltage_profile();
    
    printf("\n========== PSU Configuration ==========\n");
    printf("Battery Type:     %dV (%dS LiFePO4)\n", 
           (int)profile->nominal_voltage, profile->cells_in_series);
    printf("Battery Capacity: %.1f Ah\n", g_psu_config.battery_capacity_ah);
    printf("Site ID:          %s\n", g_psu_config.site_identifier);
    printf("SNMP Trap Dest:   %s:%d\n", 
           g_psu_config.snmp_trap_dest, g_psu_config.snmp_trap_port);
    printf("\nVoltage Thresholds:\n");
    printf("  Charge:         %.2f V\n", profile->charge_voltage);
    printf("  Warning (30%%):  %.2f V\n", g_psu_config.voltage_warning);
    printf("  Critical (20%%): %.2f V\n", g_psu_config.voltage_critical);
    printf("  Emergency (10%%):%.2f V\n", g_psu_config.voltage_cutoff_low);
    printf("\nTrap Warnings Enabled:\n");
    printf("  60min: %s  30min: %s  15min: %s  5min: %s\n",
           g_psu_config.warning_60min_enabled ? "ON" : "OFF",
           g_psu_config.warning_30min_enabled ? "ON" : "OFF",
           g_psu_config.warning_15min_enabled ? "ON" : "OFF",
           g_psu_config.warning_5min_enabled ? "ON" : "OFF");
    printf("  SOC 30%%: %s  20%%: %s  10%%: %s\n",
           g_psu_config.warning_soc30_enabled ? "ON" : "OFF",
           g_psu_config.warning_soc20_enabled ? "ON" : "OFF",
           g_psu_config.warning_soc10_enabled ? "ON" : "OFF");
    printf("=======================================\n\n");
}