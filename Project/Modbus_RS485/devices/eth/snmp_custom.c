/********************************************************************************************
 * SNMP Custom Module (Enterprise 99999)
 * - MIB tối thiểu (VIN/VOUT/VSET/IOUT/FAULT)
 * - Trap fault thay đổi (AC_FAIL/OP_OFF/OTP/OLP) + cleared
 * - Trap theo SPEC 5.1–5.2: runtime/SOC/temp/overload/hw/bms/emergency
 ********************************************************************************************/
#include "snmp_custom.h"

/* ===================================================================== */
/*                          MIB GETTERS                                   */
/* ===================================================================== */
static void get_input_voltage(void *ptr, uint8_t *len) {
  int32_t val = (int32_t)(g_vin * 100);   // 230.5V -> 23050
  *(int32_t*)ptr = val; *len = sizeof(val);
}
static void get_output_voltage(void *ptr, uint8_t *len) {
  int32_t val = (int32_t)(g_vout * 100);
  *(int32_t*)ptr = val; *len = sizeof(val);
}
static void get_set_voltage(void *ptr, uint8_t *len) {
  int32_t val = (int32_t)(g_vset * 100);
  *(int32_t*)ptr = val; *len = sizeof(val);
}
static void get_output_current(void *ptr, uint8_t *len) {
  int32_t val = (int32_t)(g_cc * 1000);   // A -> mA
  *(int32_t*)ptr = val; *len = sizeof(val);
}
static void get_fault_flags(void *ptr, uint8_t *len) {
  /* Trả về 32-bit INTEGER cho an toàn với nhiều stack */
  int32_t val32 = (int32_t)g_fault_status;
  *(int32_t*)ptr = val32; *len = sizeof(val32);
}

/* ===================================================================== */
/*                          MIB TABLE                                     */
/* ===================================================================== */
static uint8_t OID_VIN[16], OID_VOUT[16], OID_VSET[16], OID_IOUT[16], OID_FAULT[16];

dataEntryType snmpData[] =
{
  {0,{0}, SNMPDTYPE_INTEGER, 4, {""}, get_input_voltage,  NULL},  // VIN
  {0,{0}, SNMPDTYPE_INTEGER, 4, {""}, get_output_voltage, NULL},  // VOUT
  {0,{0}, SNMPDTYPE_INTEGER, 4, {""}, get_set_voltage,    NULL},  // VSET
  {0,{0}, SNMPDTYPE_INTEGER, 4, {""}, get_output_current, NULL},  // IOUT
  {0,{0}, SNMPDTYPE_INTEGER, 4, {""}, get_fault_flags,    NULL},  // FAULT
};
const int32_t maxData = (int32_t)(sizeof(snmpData)/sizeof(snmpData[0]));

/* ===================================================================== */
/*                        OID UTILITIES (BER-128)                         */
/* ===================================================================== */
static uint8_t ber_encode_base128(uint32_t v, uint8_t *out) {
  uint8_t tmp[5]; int n=0;
  do { tmp[n++] = v & 0x7F; v >>= 7; } while (v);
  for (int i=n-1, j=0; i>=0; --i, ++j) out[j] = tmp[i] | (i?0x80:0x00);
  return (uint8_t)n;
}
static uint8_t write_enterprise_root(uint8_t *buf) {
  /* 1.3.6.1.4.1.<ENTERPRISE_ID> */
  uint8_t *p=buf;
  *p++=0x2b; *p++=6; *p++=1; *p++=4; *p++=1;
  p += ber_encode_base128(ENTERPRISE_ID, p);
  return (uint8_t)(p-buf);
}
static uint8_t build_oid_branch(uint8_t *buf, uint8_t leaf) {
  uint8_t len = write_enterprise_root(buf);
  buf[len++] = 1;      // group 1 (PSU basic)
  buf[len++] = leaf;   // index
  return len;
}
void snmp_custom_init_oids(void) {
  snmpData[0].oidlen = build_oid_branch(OID_VIN,   1); memcpy(snmpData[0].oid, OID_VIN,   snmpData[0].oidlen);
  snmpData[1].oidlen = build_oid_branch(OID_VOUT,  2); memcpy(snmpData[1].oid, OID_VOUT,  snmpData[1].oidlen);
  snmpData[2].oidlen = build_oid_branch(OID_VSET,  3); memcpy(snmpData[2].oid, OID_VSET,  snmpData[2].oidlen);
  snmpData[3].oidlen = build_oid_branch(OID_IOUT,  4); memcpy(snmpData[3].oid, OID_IOUT,  snmpData[3].oidlen);
  snmpData[4].oidlen = build_oid_branch(OID_FAULT, 5); memcpy(snmpData[4].oid, OID_FAULT, snmpData[4].oidlen);
}
__attribute__((constructor)) static void _snmp_custom_ctor(void){ snmp_custom_init_oids(); }

/* ===================================================================== */
/*                         TRAP SEND WRAPPER                              */
/* ===================================================================== */
void snmp_send_trap_custom(uint8_t *managerIP, uint8_t *agentIP, uint8_t trap_code)
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
  fault_flags_t f    = g_fault_flags;

  if (last_fault == 0xFFFF) { last_fault = new_fault; return; }
  uint16_t changed = (uint16_t)(last_fault ^ new_fault);
  if (!changed) return;

  printf("[SNMP] Fault changed: old=0x%04X new=0x%04X\n", last_fault, new_fault);

  /* AC_FAIL (Power Failure/Restored) */
  if (changed & (1u<<BIT_ACFAIL)) {
    if (f.ac_fail) snmp_send_trap_custom(managerIP, agentIP, TRAP_POWER_FAILURE);
    else           snmp_send_trap_custom(managerIP, agentIP, TRAP_POWER_RESTORED);
  }
  /* OP_OFF (Output Disabled/Restored) */
  if (changed & (1u<<BIT_OPOFF)) {
    if (f.op_off) snmp_send_trap_custom(managerIP, agentIP, TRAP_OUTPUT_DISABLED);
    else          snmp_send_trap_custom(managerIP, agentIP, TRAP_OUTPUT_RESTORED);
  }
  /* OTP set/cleared */
  if (changed & (1u<<BIT_OTP)) {
    if (f.otp) snmp_send_trap_custom(managerIP, agentIP, TRAP_OVER_TEMPERATURE);
    else       snmp_send_trap_custom(managerIP, agentIP, TRAP_OTP_CLEARED);
  }
  /* OLP set/cleared */
  if (changed & (1u<<BIT_OLP)) {
    if (f.olp) snmp_send_trap_custom(managerIP, agentIP, TRAP_OVER_CURRENT);
    else       snmp_send_trap_custom(managerIP, agentIP, TRAP_OCP_CLEARED);
  }

  last_fault = new_fault;
}

/* ===================================================================== */
/*            TRAP: SPEC 5.1–5.2 (runtime / SOC / temp / misc)           */
/* ===================================================================== */

/* Tick ms (dựa trên SNMP time tick 10ms trong snmp.c) */
static inline uint32_t now_ms_for_trap(void){
  extern uint32_t getSNMPTimeTick(void);
  return getSNMPTimeTick() * 10U;
}
#define MAX_TRAP_CODE 512
static uint32_t g_last_sent_ms[MAX_TRAP_CODE];

static inline uint8_t can_send_es(uint16_t code, uint32_t tnow){
  if (code >= MAX_TRAP_CODE) return 0;
  if (tnow - g_last_sent_ms[code] < TRAP_RATELIMIT_MS) return 0;
  g_last_sent_ms[code] = tnow;
  return 1;
}
static inline void send_es(uint8_t *m, uint8_t *a, uint16_t code){
  uint32_t tnow = now_ms_for_trap();
  if (can_send_es(code, tnow)) snmp_send_trap_custom(m, a, (uint8_t)code);
}

/* Debounce cho 1 ngưỡng */
typedef struct { uint8_t latched, cnt_set, cnt_clr; } thr_t;
static thr_t thr_run60, thr_run30, thr_run15, thr_run5;
static thr_t thr_soc30, thr_soc20, thr_soc10;
static thr_t thr_tHigh, thr_tCrit;

/* Trạng thái đơn */
static uint8_t st_ac_loss=0, st_overload=0, st_hw_fault=0, st_bms_fault=0, st_emergency=0, st_full_charge=0;

static inline uint8_t debounce(thr_t *t, uint8_t set_cond, uint8_t clr_cond){
  if (!t->latched){
    if (set_cond){
      if (t->cnt_set < TRAP_DEBOUNCE_SAMPLES) t->cnt_set++;
      t->cnt_clr = 0;
      if (t->cnt_set >= TRAP_DEBOUNCE_SAMPLES){ t->cnt_set=0; t->latched=1; return 1; }
    }else{ t->cnt_set=0; t->cnt_clr=0; }
  }else{
    if (clr_cond){
      if (t->cnt_clr < TRAP_DEBOUNCE_SAMPLES) t->cnt_clr++;
      t->cnt_set=0;
      if (t->cnt_clr >= TRAP_DEBOUNCE_SAMPLES){ t->cnt_clr=0; t->latched=0; return 2; }
    }else{ t->cnt_clr=0; t->cnt_set=0; }
  }
  return 0;
}

void snmp_trap_policy_reset(void){
  memset(&thr_run60,0,sizeof(thr_run60));
  memset(&thr_run30,0,sizeof(thr_run30));
  memset(&thr_run15,0,sizeof(thr_run15));
  memset(&thr_run5 ,0,sizeof(thr_run5 ));
  memset(&thr_soc30,0,sizeof(thr_soc30));
  memset(&thr_soc20,0,sizeof(thr_soc20));
  memset(&thr_soc10,0,sizeof(thr_soc10));
  memset(&thr_tHigh,0,sizeof(thr_tHigh));
  memset(&thr_tCrit,0,sizeof(thr_tCrit));
  st_ac_loss = st_overload = st_hw_fault = st_bms_fault = st_emergency = st_full_charge = 0;
  memset(g_last_sent_ms, 0, sizeof(g_last_sent_ms));
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
