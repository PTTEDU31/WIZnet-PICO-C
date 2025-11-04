#include "psu_data.h"
#include "psu_config.h"  // Include config
#include "pico/stdlib.h"
#include <math.h>

static psu_data_t g_psu;
static mutex_t g_psu_mutex;

void psu_data_init(void)
{
    mutex_init(&g_psu_mutex);
    g_psu.vin = g_psu.vout = g_psu.vset = g_psu.iout = g_psu.cc = g_psu.temp = 0.0f;
    g_psu.batt_voltage = g_psu.batt_current = g_psu.batt_soc = g_psu.batt_runtime = 0.0f;
    g_psu.batt_capacity = g_psu_config.battery_capacity_ah; // Load from config
    g_psu.batt_temp = 0.0f;
    g_psu.fault.raw = 0x00;
    g_psu.batt_flags.raw = 0x00;
}

// ====================================================
// Update / set
// ====================================================
void psu_data_update(const psu_data_t *new_data)
{
    if (!new_data) return;
    mutex_enter_blocking(&g_psu_mutex);
    g_psu = *new_data;
    
    // Auto-calculate SOC from voltage if not provided
    if (g_psu.batt_soc <= 0.0f || g_psu.batt_soc > 100.0f) {
        g_psu.batt_soc = psu_config_voltage_to_soc(g_psu.batt_voltage);
    }
    
    // Auto-calculate runtime if load current available
    if (g_psu.iout > 0.1f && g_psu.batt_capacity > 0) {
        g_psu.batt_runtime = psu_calculate_runtime_minutes(
            g_psu.batt_capacity, 
            g_psu.batt_soc, 
            g_psu.iout
        );
    }
    
    mutex_exit(&g_psu_mutex);
}

void psu_data_set_fault(fault_flags_t fault)
{
    mutex_enter_blocking(&g_psu_mutex);
    g_psu.fault = fault;
    mutex_exit(&g_psu_mutex);
}

void psu_data_set_batt_flags(battery_flags_t flags)
{
    mutex_enter_blocking(&g_psu_mutex);
    g_psu.batt_flags = flags;
    mutex_exit(&g_psu_mutex);
}

// ====================================================
// Read snapshot (thread-safe)
// ====================================================
psu_data_t psu_data_read(void)
{
    mutex_enter_blocking(&g_psu_mutex);
    psu_data_t snapshot = g_psu;
    mutex_exit(&g_psu_mutex);
    return snapshot;
}

// ====================================================
// PSU Quick accessors
// ====================================================
float psu_read_vin(void)   { return psu_data_read().vin; }
float psu_read_vout(void)  { return psu_data_read().vout; }
float psu_read_iout(void)  { return psu_data_read().iout; }
float psu_read_temp(void)  { return psu_data_read().temp; }
float psu_read_vset(void)  { return psu_data_read().vset; }
float psu_read_cc(void)    { return psu_data_read().cc; }
uint8_t psu_read_fault_raw(void) { return psu_data_read().fault.raw; }

// ====================================================
// Battery Quick accessors
// ====================================================
float psu_read_batt_voltage(void)  { return psu_data_read().batt_voltage; }
float psu_read_batt_current(void)  { return psu_data_read().batt_current; }
float psu_read_batt_soc(void)      { return psu_data_read().batt_soc; }
float psu_read_batt_runtime(void)  { return psu_data_read().batt_runtime; }
float psu_read_batt_capacity(void) { return psu_data_read().batt_capacity; }
uint8_t psu_read_batt_flags_raw(void) { return psu_data_read().batt_flags.raw; }

// ====================================================
// Runtime Calculation (SPEC 3.1)
// ====================================================
float psu_calculate_runtime_minutes(float capacity_ah, float soc_percent, float load_current)
{
    if (load_current < 0.1f) return 9999.0f; // Infinite runtime
    if (soc_percent <= 0) return 0.0f;

    float remaining_ah = capacity_ah * (soc_percent / 100.0f);
    float runtime_hr = remaining_ah / load_current;
    return runtime_hr * 60.0f; // Convert to minutes
}

// ====================================================
// Voltage-to-SOC Conversion (now uses config)
// ====================================================
float lifepo4_voltage_to_soc(float voltage)
{
    return psu_config_voltage_to_soc(voltage);
}

// ====================================================
// Battery Status Assessment
// ====================================================
uint8_t psu_assess_battery_status(void)
{
    psu_data_t psu = psu_data_read();
    const voltage_profile_t *profile = psu_config_get_voltage_profile();
    
    // Emergency: voltage below cutoff or SOC <= 10%
    if (psu.batt_voltage < profile->discharge_cutoff || psu.batt_soc <= 10.0f) {
        return 4; // Emergency
    }
    
    // Critical: SOC <= 20% or runtime <= 15min
    if (psu.batt_soc <= 20.0f || psu.batt_runtime <= 15.0f) {
        return 3; // Critical
    }
    
    // Warning: SOC <= 30% or runtime <= 30min
    if (psu.batt_soc <= 30.0f || psu.batt_runtime <= 30.0f) {
        return 2; // Warning
    }
    
    return 1; // Normal
}

// ====================================================
// Auto-Shutdown Check
// ====================================================
uint8_t psu_should_emergency_shutdown(void)
{
    if (!g_psu_config.enable_auto_shutdown) return 0;
    
    psu_data_t psu = psu_data_read();
    const voltage_profile_t *profile = psu_config_get_voltage_profile();
    
    // Shutdown if voltage drops below emergency cutoff
    if (psu.batt_voltage > 0.1f && psu.batt_voltage < profile->emergency_cutoff) {
        return 1;
    }
    
    // Shutdown if SOC critically low
    if (psu.batt_soc <= 5.0f) {
        return 1;
    }
    
    return 0;
}