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
    int32_t val = (int32_t)(g_vset * 100);
    *(int32_t *)ptr = val;
    *len = sizeof(val);
}

static void get_temp(void *ptr, uint8_t *len)
{
    *(uint16_t *)ptr = g_fault_status;
    *len = sizeof(g_fault_status);
}

/* ===================================================================== */
/*                          MIB TABLE                                     */
/* ===================================================================== */
static uint8_t OID_VIN[16], OID_VOUT[16], OID_VSET[16], OID_IOUT[16], OID_FAULT[16];

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
const int32_t maxData = (int32_t)(sizeof(snmpData)/sizeof(snmpData[0]));

/* =========================== */
/*     Trap Sending Logic      */
/* =========================== */
static uint16_t last_fault = 0xFFFF;

static void snmp_send_trap_custom(uint8_t *managerIP, uint8_t *agentIP,
                                  uint8_t trap_num)
{
  dataEntryType enterprise_oid = {0};
  enterprise_oid.oidlen = write_enterprise_root(enterprise_oid.oid);
  printf("[SNMP] enterprise trap code=%u\n", trap_code);
  snmp_sendTrap(managerIP, agentIP, (int8_t*)COMMUNITY,
                enterprise_oid, 6 /* enterpriseSpecific */, trap_code, 0);
}

/* ===================================================================== */
/*                        WARM START TRAP                                 */
/* ===================================================================== */
void initial_Trap(uint8_t *managerIP, uint8_t *agentIP)
{
  dataEntryType enterprise_oid = {0};
  enterprise_oid.oidlen = write_enterprise_root(enterprise_oid.oid);
  snmp_sendTrap(managerIP, agentIP, (int8_t*)COMMUNITY,
                enterprise_oid, SNMPTRAP_WARMSTART, 0 /* specific */, 0);
}

/* ===================================================================== */
/*                 TRAP: FAULT CHANGE (AC/OP_OFF/OTP/OLP)                 */
/* ===================================================================== */
static uint16_t last_fault = 0xFFFF;
void snmp_trap_state_reset(void){ last_fault = 0xFFFF; }

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

/* Mọi thứ an toàn? -> bắn SystemNormal */
static inline uint8_t is_all_safe(uint8_t ac_present,
                                  float batt_v, float soc_pct, float runtime_min, float temp_c,
                                  uint8_t overload, uint8_t hw_fault, uint8_t bms_fault)
{
  uint8_t safe = 1;
  if (!ac_present) safe = 0;
  if (temp_c > (TEMP_HIGH_C - 2.0f)) safe = 0;            // về < 48°C
  if (overload) safe = 0;
  if (hw_fault || bms_fault) safe = 0;
  if (batt_v < (VBAT_EMERGENCY_V + 0.5f)) safe = 0;       // >48.5V
  if (soc_pct < (30.0f + SOC_HYS_PCT)) safe = 0;          // >32%
  if (runtime_min < (60.0f + RUNTIME_HYS_MIN)) safe = 0;  // >62’
  return safe;
}

void snmp_trap_process_spec(uint8_t *managerIP, uint8_t *agentIP,
                            uint8_t ac_present,
                            float batt_v, float soc_pct, float runtime_min, float temp_c,
                            uint8_t overload, uint8_t hw_fault, uint8_t bms_fault)
{
  /* 0) Fully charged (Informational, 1 lần khi ~100%) */
  if (ac_present && soc_pct >= 99.0f && !st_full_charge) {
    send_es(managerIP, agentIP, ES_TRAP_BATTERY_FULLY_CHARGED);
    st_full_charge = 1;
  }
  if (!ac_present || soc_pct < 95.0f) st_full_charge = 0;

  /* 1) AC Loss / Restored */
  if (!ac_present && !st_ac_loss) {
    send_es(managerIP, agentIP, ES_TRAP_AC_POWER_LOSS);
    st_ac_loss = 1;
    /* reset latch runtime khi vừa mất AC */
    thr_run60.latched = thr_run30.latched = thr_run15.latched = thr_run5.latched = 0;
    thr_run60.cnt_set = thr_run30.cnt_set = thr_run15.cnt_set = thr_run5.cnt_set = 0;
    thr_run60.cnt_clr = thr_run30.cnt_clr = thr_run15.cnt_clr = thr_run5.cnt_clr = 0;
  }
  if (ac_present && st_ac_loss) {
    send_es(managerIP, agentIP, ES_TRAP_AC_POWER_RESTORED);
    st_ac_loss = 0;
  }

  /* 2) Temperature */
  uint8_t e = debounce(&thr_tHigh, (temp_c > TEMP_HIGH_C), (temp_c < (TEMP_HIGH_C - 2.0f)));
  if (e == 1) send_es(managerIP, agentIP, ES_TRAP_TEMPERATURE_HIGH);

  e = debounce(&thr_tCrit, (temp_c > TEMP_CRIT_C), (temp_c < (TEMP_CRIT_C - 2.0f)));
  if (e == 1) send_es(managerIP, agentIP, ES_TRAP_TEMPERATURE_CRITICAL);

  /* 3) Overload */
  if (overload && !st_overload) { send_es(managerIP, agentIP, ES_TRAP_OVERLOAD_CONDITION); st_overload = 1; }
  if (!overload && st_overload)  { st_overload = 0; }

  /* 4) Runtime thresholds – on-battery */
  if (!ac_present) {
    e = debounce(&thr_run5,  (runtime_min <=  5.0f), (runtime_min >= ( 5.0f + RUNTIME_HYS_MIN))); if (e==1) send_es(managerIP, agentIP, ES_TRAP_RUNTIME_5MIN);
    e = debounce(&thr_run15, (runtime_min <= 15.0f), (runtime_min >= (15.0f + RUNTIME_HYS_MIN))); if (e==1 && !thr_run5.latched)  send_es(managerIP, agentIP, ES_TRAP_RUNTIME_15MIN);
    e = debounce(&thr_run30, (runtime_min <= 30.0f), (runtime_min >= (30.0f + RUNTIME_HYS_MIN))); if (e==1 && !thr_run15.latched && !thr_run5.latched) send_es(managerIP, agentIP, ES_TRAP_RUNTIME_30MIN);
    e = debounce(&thr_run60, (runtime_min <= 60.0f), (runtime_min >= (60.0f + RUNTIME_HYS_MIN))); if (e==1 && !thr_run30.latched && !thr_run15.latched && !thr_run5.latched) send_es(managerIP, agentIP, ES_TRAP_RUNTIME_60MIN);
  }

  /* 5) SOC thresholds – ưu tiên mốc thấp hơn */
  e = debounce(&thr_soc10, (soc_pct <= 10.0f), (soc_pct >= (10.0f + SOC_HYS_PCT))); if (e==1) send_es(managerIP, agentIP, ES_TRAP_SOC_10PCT);
  e = debounce(&thr_soc20, (soc_pct <= 20.0f), (soc_pct >= (20.0f + SOC_HYS_PCT))); if (e==1 && !thr_soc10.latched) send_es(managerIP, agentIP, ES_TRAP_SOC_20PCT);
  e = debounce(&thr_soc30, (soc_pct <= 30.0f), (soc_pct >= (30.0f + SOC_HYS_PCT))); if (e==1 && !thr_soc20.latched && !thr_soc10.latched) send_es(managerIP, agentIP, ES_TRAP_SOC_30PCT);

  /* 6) Emergency */
  if ((batt_v > 0.1f && batt_v < VBAT_EMERGENCY_V) || soc_pct <= 10.0f) {
    if (!st_emergency) { send_es(managerIP, agentIP, ES_TRAP_BATTERY_EMERGENCY_SHUTDOWN); st_emergency = 1; }
  } else if (st_emergency && (batt_v > VBAT_EMERGENCY_V + 0.5f) && soc_pct > 12.0f) {
    st_emergency = 0;
  }

  if (hw_fault && !st_hw_fault) { send_es(managerIP, agentIP, ES_TRAP_HARDWARE_FAULT); st_hw_fault = 1; }
  if (!hw_fault && st_hw_fault)  { st_hw_fault = 0; }
  if (bms_fault && !st_bms_fault){ send_es(managerIP, agentIP, ES_TRAP_BMS_FAULT);     st_bms_fault = 1; }
  if (!bms_fault && st_bms_fault){ st_bms_fault = 0; }
  /* Nếu có cờ shutdown nhiệt riêng, bắn ES_TRAP_OVERTEMP_SHUTDOWN ở đây. */

  /* 7) System Normal */
  static uint8_t sent_normal = 0;
  if (is_all_safe(ac_present, batt_v, soc_pct, runtime_min, temp_c, overload, hw_fault, bms_fault)) {
    if (!sent_normal) { send_es(managerIP, agentIP, ES_TRAP_SYSTEM_NORMAL); sent_normal = 1; }
  } else {
    sent_normal = 0;
  }
}
