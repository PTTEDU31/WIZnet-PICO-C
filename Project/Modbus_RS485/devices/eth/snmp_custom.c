/********************************************************************************************
 * SNMP Custom Module (Enterprise 99999)
 * - Lấy dữ liệu từ module PSU (psu_data.c)
 * - Gửi trap SNMP khi fault thay đổi
 ********************************************************************************************/
#include "snmp_custom.h"
#include "../psu_data/psu_data.h"

/* =========================== */
/*     Getter Callbacks        */
/* =========================== */
static void get_input_voltage(void *ptr, uint8_t *len)
{
    psu_data_t psu = psu_data_read();
    int32_t val = (int32_t)(psu.vin * 100); // ví dụ 230.5V → 23050
    *(int32_t *)ptr = val;
    *len = sizeof(val);
}

static void get_output_voltage(void *ptr, uint8_t *len)
{
    psu_data_t psu = psu_data_read();
    int32_t val = (int32_t)(psu.vout * 100);
    *(int32_t *)ptr = val;
    *len = sizeof(val);
}

static void get_output_current(void *ptr, uint8_t *len)
{
    psu_data_t psu = psu_data_read();
    int32_t val = (int32_t)(psu.iout * 1000); // 1.23A → 1230mA
    *(int32_t *)ptr = val;
    *len = sizeof(val);
}

static void get_set_voltage(void *ptr, uint8_t *len)
{
    psu_data_t psu = psu_data_read();
    int32_t val = (int32_t)(psu.vset * 100);
    *(int32_t *)ptr = val;
    *len = sizeof(val);
}

static void get_temp(void *ptr, uint8_t *len)
{
    psu_data_t psu = psu_data_read();
    int32_t val = (int32_t)(psu.temp * 100);
    *(int32_t *)ptr = val;
    *len = sizeof(val);
}

static void get_fault_flags(void *ptr, uint8_t *len)
{
    psu_data_t psu = psu_data_read();
    *(uint8_t *)ptr = psu.fault.raw;
    *len = sizeof(psu.fault.raw);
}

/* =========================== */
/*     SNMP Data Table (MIB)   */
/* =========================== */
dataEntryType snmpData[] =
{
    // VIN
    {10, {0x2b,6,1,4,1,0x86,0x9F,0x4F,1,1},
     SNMPDTYPE_INTEGER, 4, {""},
     get_input_voltage, NULL},

    // VOUT
    {10, {0x2b,6,1,4,1,0x86,0x9F,0x4F,1,2},
     SNMPDTYPE_INTEGER, 4, {""},
     get_output_voltage, NULL},

    // VSET
    {10, {0x2b,6,1,4,1,0x86,0x9F,0x4F,1,3},
     SNMPDTYPE_INTEGER, 4, {""},
     get_set_voltage, NULL},

    // IOUT
    {10, {0x2b,6,1,4,1,0x86,0x9F,0x4F,1,4},
     SNMPDTYPE_INTEGER, 4, {""},
     get_output_current, NULL},

    // TEMP
    {10, {0x2b,6,1,4,1,0x86,0x9F,0x4F,1,5},
     SNMPDTYPE_INTEGER, 4, {""},
     get_temp, NULL},

    // FAULT FLAGS (raw byte)
    {10, {0x2b,6,1,4,1,0x86,0x9F,0x4F,1,6},
     SNMPDTYPE_INTEGER, 1, {""},
     get_fault_flags, NULL},
};

const int32_t maxData = (sizeof(snmpData) / sizeof(dataEntryType));

/* =========================== */
/*     Trap Sending Logic      */
/* =========================== */
static uint8_t last_fault_raw = 0xFF;

static void snmp_send_trap_custom(uint8_t *managerIP, uint8_t *agentIP,
                                  uint8_t trap_num)
{
    dataEntryType enterprise_oid = {
        10, {0x2b,6,1,4,1,0x86,0x9F,0x4F,2,trap_num},
        SNMPDTYPE_OBJ_ID, 10,
        {"\x2b\x06\x01\x04\x01\x86\x9F\x4F\x02\x00"},
        NULL, NULL
    };
    printf("[SNMP] Trap sent #%d\n", trap_num);
    snmp_sendTrap(managerIP, agentIP, (int8_t*)COMMUNITY,
                  enterprise_oid, 6, trap_num, 0);
}

/* =========================== */
/*     Process Fault Change    */
/* =========================== */
void snmp_process_fault_trap(uint8_t *managerIP, uint8_t *agentIP)
{
    psu_data_t psu = psu_data_read();
    uint8_t new_fault = psu.fault.raw;
    fault_flags_t f = psu.fault;

    if (last_fault_raw == 0xFF) { // lần đầu khởi động
        last_fault_raw = new_fault;
        return;
    }

    uint8_t changed = last_fault_raw ^ new_fault;
    if (!changed) return; // không thay đổi

    printf("[SNMP] Fault changed: old=0x%02X new=0x%02X\n", last_fault_raw, new_fault);

    // OTP (Over Temperature)
    if ((changed & (1 << 1)) && f.bits.otp)
        snmp_send_trap_custom(managerIP, agentIP, 3);

    // OLP (Overload / Overcurrent)
    if ((changed & (1 << 3)) && f.bits.olp)
        snmp_send_trap_custom(managerIP, agentIP, 4);

    // AC Fail hoặc Output Off
    if (changed & ((1 << 5) | (1 << 6))) {
        if (f.bits.ac_fail || f.bits.op_off)
            snmp_send_trap_custom(managerIP, agentIP, 1); // Power failure
        else
            snmp_send_trap_custom(managerIP, agentIP, 2); // Power restored
    }

    last_fault_raw = new_fault;
}

/* ===========================
 * Warm Start Trap
 * =========================== */
void initial_Trap(uint8_t *managerIP, uint8_t *agentIP)
{
    dataEntryType enterprise_oid = {
        10, {0x2b,6,1,4,1,0x86,0x9F,0x4F,2,0},
        SNMPDTYPE_OBJ_ID, 10,
        {"\x2b\x06\x01\x04\x01\x86\x9F\x4F\x02\x00"},
        NULL, NULL
    };
    snmp_sendTrap(managerIP, agentIP, (int8_t*)COMMUNITY,
                  enterprise_oid, SNMPTRAP_WARMSTART, 0, 0);
}
