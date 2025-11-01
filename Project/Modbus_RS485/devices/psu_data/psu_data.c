#include "psu_data.h"
#include "pico/stdlib.h"

static psu_data_t g_psu;
static mutex_t g_psu_mutex;

void psu_data_init(void)
{
    mutex_init(&g_psu_mutex);
    g_psu.vin = g_psu.vout = g_psu.vset = g_psu.iout = g_psu.cc = g_psu.temp = 0.0f;
    g_psu.fault.raw = 0x00;
}

// ====================================================
// Update / set
// ====================================================

// Cập nhật toàn bộ dữ liệu PSU
void psu_data_update(const psu_data_t *new_data)
{
    if (!new_data) return;
    mutex_enter_blocking(&g_psu_mutex);
    g_psu = *new_data;
    mutex_exit(&g_psu_mutex);
}

// Cập nhật riêng trạng thái lỗi
void psu_data_set_fault(fault_flags_t fault)
{
    mutex_enter_blocking(&g_psu_mutex);
    g_psu.fault = fault;
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
// Quick accessors
// ====================================================
float psu_read_vin(void)   { psu_data_t p = psu_data_read(); return p.vin; }
float psu_read_vout(void)  { psu_data_t p = psu_data_read(); return p.vout; }
float psu_read_iout(void)  { psu_data_t p = psu_data_read(); return p.iout; }
float psu_read_temp(void)  { psu_data_t p = psu_data_read(); return p.temp; }
float psu_read_vset(void)  { psu_data_t p = psu_data_read(); return p.vset; }
float psu_read_cc(void)    { psu_data_t p = psu_data_read(); return p.cc; }
uint8_t psu_read_fault_raw(void) { psu_data_t p = psu_data_read(); return p.fault.raw; }
