#ifndef PSU_DATA_H
#define PSU_DATA_H

#include <stdint.h>
#include "pico/sync.h"

// =============================================================
// Fault flags (7 errors, packed in 1 byte bitfield)
// =============================================================
typedef union
{
    struct
    {
        uint8_t fan_fail      : 1;  // Bit 0 - Fan failure
        uint8_t otp           : 1;  // Bit 1 - Over temperature
        uint8_t ovp           : 1;  // Bit 2 - Over voltage
        uint8_t olp           : 1;  // Bit 3 - Over load
        uint8_t short_circuit : 1;  // Bit 4 - Short circuit
        uint8_t ac_fail       : 1;  // Bit 5 - AC fail
        uint8_t op_off        : 1;  // Bit 6 - Output off
        uint8_t reserved      : 1;  // Bit 7 - Reserved
    } bits;
    uint8_t raw;
} fault_flags_t;

// =============================================================
// Battery status flags
// =============================================================
typedef union
{
    struct
    {
        uint8_t charging      : 1;  // Charging
        uint8_t discharging   : 1;  // Discharging
        uint8_t full          : 1;  // Full (100%)
        uint8_t low           : 1;  // Low (< 20%)
        uint8_t critical      : 1;  // Critical (< 10%)
        uint8_t bms_fault     : 1;  // BMS fault
        uint8_t reserved      : 2;
    } bits;
    uint8_t raw;
} battery_flags_t;

// =============================================================
// PSU + Battery live data structure
// =============================================================
typedef struct
{
    // PSU section
    float vin;      // Input Voltage (V)
    float vout;     // Output Voltage (V)
    float vset;     // Voltage Setpoint (V)
    float iout;     // Output Current (A)
    float cc;       // Constant-current limit (A)
    float temp;     // PSU Temperature (°C)
    fault_flags_t fault;

    // Battery section
    float batt_voltage;   // Battery voltage (V)
    float batt_current;   // Current (+ = charge, - = discharge)
    float batt_soc;       // State of Charge (%)
    float batt_runtime;   // Estimated runtime (minutes)
    float batt_capacity;  // Nominal capacity (Ah)
    float batt_temp;      // Battery temperature (°C)
    battery_flags_t batt_flags;
} psu_data_t;

// =============================================================
// PSU Data module API
// =============================================================

/**
 * Initialize PSU data module
 */
void psu_data_init(void);

/**
 * Update all PSU data (thread-safe)
 * Auto-calculates SOC and runtime if needed
 */
void psu_data_update(const psu_data_t *new_data);

/**
 * Set fault flags
 */
void psu_data_set_fault(fault_flags_t fault);

/**
 * Set battery flags
 */
void psu_data_set_batt_flags(battery_flags_t flags);

/**
 * Read snapshot of current data (thread-safe)
 */
psu_data_t psu_data_read(void);

// =============================================================
// PSU Quick Getters
// =============================================================
float psu_read_vin(void);
float psu_read_vout(void);
float psu_read_iout(void);
float psu_read_temp(void);
float psu_read_vset(void);
float psu_read_cc(void);
uint8_t psu_read_fault_raw(void);

// =============================================================
// Battery Quick Getters
// =============================================================
float psu_read_batt_voltage(void);
float psu_read_batt_current(void);
float psu_read_batt_soc(void);
float psu_read_batt_runtime(void);
float psu_read_batt_capacity(void);
uint8_t psu_read_batt_flags_raw(void);

// =============================================================
// Calculation Functions
// =============================================================

/**
 * Calculate battery runtime in minutes (SPEC 3.1)
 * @param capacity_ah - Battery capacity in Ah
 * @param soc_percent - Current state of charge (0-100%)
 * @param load_current - Load current in A
 * @return Runtime in minutes
 */
float psu_calculate_runtime_minutes(float capacity_ah, float soc_percent, float load_current);

/**
 * Convert voltage to SOC% (uses configured battery type)
 * @param voltage - Battery voltage in V
 * @return SOC percentage (0-100%)
 */
float lifepo4_voltage_to_soc(float voltage);

/**
 * Assess battery status (1=normal, 2=warning, 3=critical, 4=emergency)
 */
uint8_t psu_assess_battery_status(void);

/**
 * Check if emergency shutdown is required
 * @return 1 if should shutdown, 0 otherwise
 */
uint8_t psu_should_emergency_shutdown(void);

#endif // PSU_DATA_H