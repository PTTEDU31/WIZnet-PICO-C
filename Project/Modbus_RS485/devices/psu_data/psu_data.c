#include "psu_data.h"
#include "pico/stdlib.h"
#include <math.h>

static psu_data_t g_psu;
static mutex_t g_psu_mutex;

void psu_data_init(void)
{
    mutex_init(&g_psu_mutex);
    g_psu.vin = g_psu.vout = g_psu.vset = g_psu.iout = g_psu.cc = g_psu.temp = 0.0f;
    g_psu.batt_voltage = g_psu.batt_current = g_psu.batt_soc = g_psu.batt_runtime = g_psu.batt_capacity = 0.0f;
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
// Runtime Calculation
// ====================================================
float psu_calculate_runtime_minutes(float capacity_ah, float soc_percent, float load_current)
{
    if (load_current < 0.1f) return 9999.0f; // vô hạn
    if (soc_percent <= 0) return 0.0f;

    float runtime_hr = (capacity_ah * (soc_percent / 100.0f)) / load_current;
    return runtime_hr * 60.0f; // phút
}
