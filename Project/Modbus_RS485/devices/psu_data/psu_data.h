#ifndef PSU_DATA_H
#define PSU_DATA_H

#include <stdint.h>
#include "pico/sync.h"

// =============================================================
// Fault flags (7 lỗi, gói gọn trong 1 byte bitfield)
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
    uint8_t raw;                    // Raw byte access
} fault_flags_t;

// =============================================================
// PSU live data structure
// =============================================================
typedef struct
{
    float vin;      // Voltage In (V)
    float vout;     // Voltage Out (V)
    float vset;     // Voltage Setpoint (V)
    float iout;     // Output Current (A)
    float cc;       // Constant-current limit (A)
    float temp;     // Temperature (°C)
    fault_flags_t fault;  // Fault flags bitfield
} psu_data_t;

// =============================================================
// PSU Data module API
// =============================================================

// Gọi 1 lần trong main() để khởi tạo mutex
void psu_data_init(void);

// Cập nhật toàn bộ dữ liệu PSU
void psu_data_update(const psu_data_t *new_data);

// Cập nhật riêng lỗi
void psu_data_set_fault(fault_flags_t fault);

// Đọc snapshot dữ liệu PSU (thread-safe)
psu_data_t psu_data_read(void);

// Truy cập nhanh cho Web/SNMP
float psu_read_vin(void);
float psu_read_vout(void);
float psu_read_iout(void);
float psu_read_temp(void);
float psu_read_vset(void);
float psu_read_cc(void);
uint8_t psu_read_fault_raw(void);

#endif // PSU_DATA_H
