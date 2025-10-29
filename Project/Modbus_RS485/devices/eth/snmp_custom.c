/********************************************************************************************
 * SNMP Custom Module (Enterprise 99999)
 * - Lấy dữ liệu từ Modbus: g_vin, g_vout, g_vset, g_cc
 * - Tự động gửi trap SNMP nếu fault thay đổi
 ********************************************************************************************/
#include "snmp_custom.h"

/* =========================== */
/*      Global Variables       */
/* =========================== */

/* =========================== */
/*     Getter Callbacks        */
/* =========================== */
static void get_input_voltage(void *ptr, uint8_t *len)
{
    int32_t val = (int32_t)(g_vin * 100); // ví dụ: 230.5V → 23050
    *(int32_t *)ptr = val;
    *len = sizeof(val);
}

static void get_output_voltage(void *ptr, uint8_t *len)
{
    int32_t val = (int32_t)(g_vout * 100);
    *(int32_t *)ptr = val;
    *len = sizeof(val);
}

static void get_output_current(void *ptr, uint8_t *len)
{
    int32_t val = (int32_t)(g_cc * 1000); // ví dụ 1.23A → 1230mA
    *(int32_t *)ptr = val;
    *len = sizeof(val);
}

static void get_set_voltage(void *ptr, uint8_t *len)
{
    int32_t val = (int32_t)(g_vset * 100);
    *(int32_t *)ptr = val;
    *len = sizeof(val);
}

static void get_fault_flags(void *ptr, uint8_t *len)
{
    *(uint16_t *)ptr = g_fault_status;
    *len = sizeof(g_fault_status);
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

    // CC
    {10, {0x2b,6,1,4,1,0x86,0x9F,0x4F,1,4},
     SNMPDTYPE_INTEGER, 4, {""},
     get_output_current, NULL},

    // Fault Flags
    {10, {0x2b,6,1,4,1,0x86,0x9F,0x4F,1,5},
     SNMPDTYPE_INTEGER, 2, {""},
     get_fault_flags, NULL},
};

const int32_t maxData = (sizeof(snmpData) / sizeof(dataEntryType));

/* =========================== */
/*     Trap Sending Logic      */
/* =========================== */
static uint16_t last_fault = 0xFFFF;

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
    uint16_t new_fault = g_fault_status;
    fault_flags_t f = g_fault_flags;

    if (last_fault == 0xFFFF) { // Lần đầu
        last_fault = new_fault;
        return;
    }

    uint16_t changed = last_fault ^ new_fault;
    if (!changed) return;

    printf("[SNMP] Fault changed: old=0x%04X new=0x%04X\n", last_fault, new_fault);

    if ((changed & (1 << 1)) && f.otp)
        snmp_send_trap_custom(managerIP, agentIP, 3); // overTemperatureTrap

    if ((changed & (1 << 3)) && f.olp)
        snmp_send_trap_custom(managerIP, agentIP, 4); // overCurrentTrap

    if (changed & ((1 << 5) | (1 << 6))) {
        if (f.ac_fail || f.op_off)
            snmp_send_trap_custom(managerIP, agentIP, 1); // powerFailureTrap
        else
            snmp_send_trap_custom(managerIP, agentIP, 2); // powerRestoredTrap
    }

    last_fault = new_fault;
}

/* =========================== */
/*     Init Default Values     */
/* =========================== */
void initTable(void)
{
    g_vin  = 0;
    g_vout = 0;
    g_vset = 0;
    g_cc   = 0;
    g_fault_status = 0;
    memset(&g_fault_flags, 0, sizeof(g_fault_flags));
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
