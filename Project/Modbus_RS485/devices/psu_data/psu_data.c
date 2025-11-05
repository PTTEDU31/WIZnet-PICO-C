#include "psu_data.h"
#include "psu_config.h" // Include config
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "string.h"
#include <math.h>
#include "psu_data_save.h"
#include "stdio.h"

static psu_data_t g_psu;
static mutex_t g_psu_mutex;

static psu_cycle_db_t g_cycles;

static inline uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

// Điểm ranh giới chu kỳ: “đổi pha” sạc↔xả
static inline uint8_t is_charging(float i) { return (i > 0.05f); }     // >0 đang nạp
static inline uint8_t is_discharging(float i) { return (i < -0.05f); } // <0 đang xả

void psu_data_init(void)
{
    mutex_init(&g_psu_mutex);
    g_psu.vin = g_psu.vout = g_psu.vset = g_psu.iout = g_psu.cc = g_psu.temp = 0.0f;
    g_psu.batt_voltage = g_psu.batt_current = g_psu.batt_soc = g_psu.batt_runtime = 0.0f;
    g_psu.batt_capacity = g_psu_config.battery_capacity_ah; // Load from config
    g_psu.batt_temp = 0.0f;
    g_psu.fault.raw = 0x00;
    g_psu.batt_flags.raw = 0x00;
    // === init cycle db ===
    memset(&g_cycles, 0, sizeof(g_cycles));
    g_cycles.last_tick_ms = now_ms();
    g_cycles.active_charging = is_charging(g_psu.batt_current);

    psu_cycles_load_from_flash();
}

void psu_cycles_print_current(void)
{
    // Lấy snapshot để in (tránh giữ lock lâu nếu bạn dùng mutex)
    psu_cycle_db_t snap = g_cycles;   // nếu có mutex: lock → memcpy → unlock

    const char *phase = snap.active_charging ? "CHARGING" : "DISCHARGING";

    printf("---- Current Cycle ----\n");
    printf("  Phase     : %s\n", phase);
    printf("  accum mAh : %.2f mAh\n", snap.accum_mAh);
    printf("  Vmax      : %.3f V\n",  snap.vmax_run);
    printf("  Imax(|I|) : %.3f A\n",  snap.imax_run);
    printf("  Tmax      : %.2f C\n",  snap.tmax_run);
    printf("  last_tick : %u ms\n",   snap.last_tick_ms);
}

void psu_cycles_print_all(void)
{
    psu_cycle_db_t snap = g_cycles;  // nếu có mutex: lock → memcpy → unlock

    printf("==== Cycle History (count=%u) ====\n", snap.count);
    if (snap.count == 0) {
        printf("  (no finished cycles)\n");
        return;
    }

    printf("  learned_avg : %.2f mAh\n",  snap.learned_capacity_mAh_avg);
    printf("  learned_best: %.2f mAh\n",  snap.learned_capacity_mAh_best);

    // Duyệt mới → cũ
    int idx = (snap.head + PSU_CYCLE_WINDOW - 1) % PSU_CYCLE_WINDOW;
    for (uint8_t n = 0; n < snap.count; ++n) {
        const psu_cycle_stat_t *r = &snap.ring[idx];
        printf("-- #%u (idx=%d) --\n", (unsigned)n, idx);
        printf("  type   : %s\n", r->charging ? "CHARGING" : "DISCHARGING");
        printf("  mAh    : %.2f\n", r->mAh);
        printf("  Vmax   : %.3f V\n", r->vmax);
        printf("  Imax   : %.3f A\n", r->imax);
        printf("  Tmax   : %.2f C\n", r->tmax);
        printf("  tstart : %u ms\n", r->start_ms);
        printf("  tend   : %u ms\n", r->end_ms);

        idx = (idx + PSU_CYCLE_WINDOW - 1) % PSU_CYCLE_WINDOW;
    }
}

void psu_cycles_print_csv(void)
{
    psu_cycle_db_t snap = g_cycles;

    printf("type,mAh,Vmax,ImAx,Tmax,start_ms,end_ms\n");
    if (snap.count == 0) return;

    int idx = (snap.head + PSU_CYCLE_WINDOW - 1) % PSU_CYCLE_WINDOW;
    for (uint8_t n = 0; n < snap.count; ++n) {
        const psu_cycle_stat_t *r = &snap.ring[idx];
        printf("%s,%.2f,%.3f,%.3f,%.2f,%u,%u\n",
               r->charging ? "CHARGE" : "DISCHARGE",
               r->mAh, r->vmax, r->imax, r->tmax, r->start_ms, r->end_ms);
        idx = (idx + PSU_CYCLE_WINDOW - 1) % PSU_CYCLE_WINDOW;
    }
}



// ====================================================
// Update / set
// ====================================================
void psu_cycles_overwrite(const psu_cycle_db_t *src)
{
    if (!src)
        return;
    // Nếu bạn dùng mutex cho g_cycles, khoá ở đây
    mutex_enter_blocking(&g_psu_mutex);
    g_cycles = *src;
    mutex_exit(&g_psu_mutex);
}

void psu_data_update(const psu_data_t *new_data)
{
    if (!new_data)
        return;
    mutex_enter_blocking(&g_psu_mutex);
    g_psu = *new_data;

    // Auto-calculate SOC from voltage if not provided
    if (g_psu.batt_soc <= 0.0f || g_psu.batt_soc > 100.0f)
    {
        g_psu.batt_soc = psu_config_voltage_to_soc(g_psu.batt_voltage);
    }

    // Auto-calculate runtime if load current available
    if (g_psu.iout > 0.1f && g_psu.batt_capacity > 0)
    {
        g_psu.batt_runtime = psu_calculate_runtime_minutes(
            g_psu.batt_capacity,
            g_psu.batt_soc,
            g_psu.iout);
    }

    // ==== Coulomb counting theo thời gian thực ====
    uint32_t now = now_ms();
    uint32_t dt_ms = now - g_cycles.last_tick_ms;
    if (dt_ms > 0 && fabsf(g_psu.batt_current) > 0.01f)
    {
        // mAh += I(A) * (dt_ms / 3600_000) * 1000
        float dmAh = g_psu.batt_current * ((float)dt_ms / 3.6e6f) * 1000.0f;
        g_cycles.accum_mAh += dmAh;

        // cập nhật cực trị đang chạy
        if (g_psu.batt_voltage > g_cycles.vmax_run)
            g_cycles.vmax_run = g_psu.batt_voltage;
        float iabs = fabsf(g_psu.batt_current);
        if (iabs > g_cycles.imax_run)
            g_cycles.imax_run = iabs;
        if (g_psu.batt_temp > g_cycles.tmax_run)
            g_cycles.tmax_run = g_psu.batt_temp;
    }
    g_cycles.last_tick_ms = now;

    // // ==== Phát hiện ranh giới chu kỳ ====
    // uint8_t charging_now = is_charging(g_psu.batt_current);
    // uint8_t dischg_now = is_discharging(g_psu.batt_current);

    // bool hit_full = (g_psu.batt_flags.bits.full || g_psu.batt_soc >= 99.0f);
    // bool hit_very_low = (g_psu.batt_soc <= 10.0f); // tuỳ chemistry

    // bool phase_flip = (g_cycles.active_charging && dischg_now) ||
    //                   (!g_cycles.active_charging && charging_now);

    // // Nếu đổi pha hoặc chạm mốc đầy/cạn -> đóng chu kỳ
    // if (phase_flip || hit_full || hit_very_low)
    // {
    //     psu_cycle_stat_t rec = {
    //         .mAh = g_cycles.accum_mAh,
    //         .vmax = g_cycles.vmax_run,
    //         .imax = g_cycles.imax_run,
    //         .tmax = g_cycles.tmax_run,
    //         .start_ms = 0, // (tuỳ bạn có muốn lưu start)
    //         .end_ms = now,
    //         .charging = g_cycles.active_charging};

    //     // Đẩy vào ring buffer
    //     g_cycles.ring[g_cycles.head] = rec;
    //     g_cycles.head = (g_cycles.head + 1) % PSU_CYCLE_WINDOW;
    //     if (g_cycles.count < PSU_CYCLE_WINDOW)
    //         g_cycles.count++;

    //     // Tính lại learned capacity (trung bình |mAh|)
    //     float sum_abs = 0.f, best = 0.f;
    //     for (uint8_t i = 0; i < g_cycles.count; ++i)
    //     {
    //         float a = fabsf(g_cycles.ring[i].mAh);
    //         sum_abs += a;
    //         if (a > best)
    //             best = a;
    //     }
    //     if (g_cycles.count > 0)
    //     {
    //         g_cycles.learned_capacity_mAh_avg = sum_abs / g_cycles.count;
    //         g_cycles.learned_capacity_mAh_best = best;
    //     }

    //     // Reset bộ đếm cho chu kỳ mới
    //     g_cycles.accum_mAh = 0.f;
    //     g_cycles.vmax_run = g_cycles.imax_run = g_cycles.tmax_run = 0.f;
    //     g_cycles.active_charging = charging_now ? 1 : 0;
    //     g_cycles.last_tick_ms = now;

        // psu_cycles_save_to_flash();
    // }

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
float psu_read_vin(void) { return psu_data_read().vin; }
float psu_read_vout(void) { return psu_data_read().vout; }
float psu_read_iout(void) { return psu_data_read().iout; }
float psu_read_temp(void) { return psu_data_read().temp; }
float psu_read_vset(void) { return psu_data_read().vset; }
float psu_read_cc(void) { return psu_data_read().cc; }
uint8_t psu_read_fault_raw(void) { return psu_data_read().fault.raw; }

// ====================================================
// Battery Quick accessors
// ====================================================
float psu_read_batt_voltage(void) { return psu_data_read().batt_voltage; }
float psu_read_batt_current(void) { return psu_data_read().batt_current; }
float psu_read_batt_soc(void) { return psu_data_read().batt_soc; }
float psu_read_batt_runtime(void) { return psu_data_read().batt_runtime; }
float psu_read_batt_capacity(void) { return psu_data_read().batt_capacity; }
uint8_t psu_read_batt_flags_raw(void) { return psu_data_read().batt_flags.raw; }
const psu_cycle_db_t *psu_cycles_get(void) { return &g_cycles; }
float psu_learned_capacity_mAh(void) { return g_cycles.learned_capacity_mAh_avg; }
float psu_learned_capacity_mAh_best(void) { return g_cycles.learned_capacity_mAh_best; }

// ====================================================
// Runtime Calculation (SPEC 3.1)
// ====================================================
float psu_calculate_runtime_minutes(float capacity_ah, float soc_percent, float load_current)
{
    if (load_current < 0.1f)
        return 9999.0f; // Infinite runtime
    if (soc_percent <= 0)
        return 0.0f;

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
    if (psu.batt_voltage < profile->discharge_cutoff || psu.batt_soc <= 10.0f)
    {
        return 4; // Emergency
    }

    // Critical: SOC <= 20% or runtime <= 15min
    if (psu.batt_soc <= 20.0f || psu.batt_runtime <= 15.0f)
    {
        return 3; // Critical
    }

    // Warning: SOC <= 30% or runtime <= 30min
    if (psu.batt_soc <= 30.0f || psu.batt_runtime <= 30.0f)
    {
        return 2; // Warning
    }

    return 1; // Normal
}

// ====================================================
// Auto-Shutdown Check
// ====================================================
uint8_t psu_should_emergency_shutdown(void)
{
    if (!g_psu_config.enable_auto_shutdown)
        return 0;

    psu_data_t psu = psu_data_read();
    const voltage_profile_t *profile = psu_config_get_voltage_profile();

    // Shutdown if voltage drops below emergency cutoff
    if (psu.batt_voltage > 0.1f && psu.batt_voltage < profile->emergency_cutoff)
    {
        return 1;
    }

    // Shutdown if SOC critically low
    if (psu.batt_soc <= 5.0f)
    {
        return 1;
    }

    return 0;
}