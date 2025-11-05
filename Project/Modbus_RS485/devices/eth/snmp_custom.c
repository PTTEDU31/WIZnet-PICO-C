/********************************************************************************************
 * SNMP Custom Module (Enterprise 99999)
 * - Lấy dữ liệu từ module PSU (psu_data.c)
 * - Gửi trap SNMP khi fault thay đổi
 ********************************************************************************************/
#include "snmp_custom.h"
#include "../psu_data/psu_data.h"
#include "app_config.h"
#include "math.h"

// static uint8_t OID_VIN[16], OID_VOUT[16], OID_VSET[16], OID_IOUT[16], OID_FAULT[16];
static uint8_t OID_TRAP_SEVERITY[16];
#define TRAP_EN_POWERFAIL(mask)   ((mask) & (1u<<0))
#define TRAP_EN_OVERTEMP(mask)    ((mask) & (1u<<1))
#define TRAP_EN_OVERCURR(mask)    ((mask) & (1u<<2))
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
  int32_t val = (int32_t)(psu.iout * 1000);
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
  uint32_t val = (uint32_t)(psu.fault.raw);
  *(uint32_t *)ptr = val;
}

// Điện áp pin (V) *100
static void get_batt_voltage(void *ptr, uint8_t *len)
{
  psu_data_t psu = psu_data_read();
  int32_t val = (int32_t)(psu.batt_voltage * 100.0f);
  *(int32_t *)ptr = val;
  *len = sizeof(val);
}

// SOC (%) *10  -> đơn vị 0.1%
static void get_batt_soc(void *ptr, uint8_t *len)
{
  psu_data_t psu = psu_data_read();
  float soc = psu.batt_soc; // 0..100 (%)
  if (soc < 0.f)
    soc = 0.f;
  if (soc > 100.f)
    soc = 100.f;
  int32_t val = (int32_t)lrintf(soc * 10.0f);
  *(int32_t *)ptr = val;
  *len = sizeof(val);
}

// Dung lượng danh định (Ah) *100
static void get_batt_capacity_ah(void *ptr, uint8_t *len)
{
  const app_config_t *cfg = app_cfg_get();
  float cap = (cfg ? cfg->psu.battery_capacity_ah : 0.0f);
  int32_t val = (int32_t)lrintf(cap * 100.0f);
  *(int32_t *)ptr = val;
  *len = sizeof(val);
}

// Dung lượng còn lại ước tính (Ah) *100  = capacity * SOC/100
static void get_batt_remaining_ah(void *ptr, uint8_t *len)
{
  psu_data_t psu = psu_data_read();
  const app_config_t *cfg = app_cfg_get();

  float cap = (cfg ? cfg->psu.battery_capacity_ah : 0.0f);
  float soc = psu.batt_soc; // %
  if (soc < 0.f)
    soc = 0.f;
  if (soc > 100.f)
    soc = 100.f;

  float remain_ah = cap * (soc / 100.0f);
  if (remain_ah < 0.f)
    remain_ah = 0.f;

  int32_t val = (int32_t)lrintf(remain_ah * 100.0f);
  *(int32_t *)ptr = val;
  *len = sizeof(val);
}

// Thời gian sử dụng còn lại (phút) = (Ah còn lại / Iout) * 60
// Nếu iout <= 0 (không tải), trả 0x7FFFFFFF (INT_MAX) như “vô cùng lớn”
static void get_batt_runtime_min(void *ptr, uint8_t *len)
{
  psu_data_t psu = psu_data_read();
  const app_config_t *cfg = app_cfg_get();

  float cap = (cfg ? cfg->psu.battery_capacity_ah : 0.0f);
  float soc = psu.batt_soc; // %
  float iout = psu.iout;    // A (đang xài)

  if (soc < 0.f)
    soc = 0.f;
  if (soc > 100.f)
    soc = 100.f;

  float remain_ah = cap * (soc / 100.0f);

  int32_t val;
  if (iout > 0.001f)
  {
    float minutes = (remain_ah / iout) * 60.0f;
    if (minutes < 0.f)
      minutes = 0.f;
    if (minutes > 2147480000.f)
      minutes = 2147480000.f; // chặn tràn
    val = (int32_t)lrintf(minutes);
  }
  else
  {
    val = 0x7FFFFFFF; // “rất lớn” khi không tải
  }

  *(int32_t *)ptr = val;
  *len = sizeof(val);
}

// Nhiệt độ pin (°C) *100 — nếu dữ liệu sẵn có
static void get_batt_temp(void *ptr, uint8_t *len)
{
  psu_data_t psu = psu_data_read();
  int32_t val = (int32_t)lrintf(psu.batt_temp * 100.0f);
  *(int32_t *)ptr = val;
  *len = sizeof(val);
}

// // Chế độ sạc của bộ nạp (ví dụ: 0=Idle,1=CC,2=CV,3=Float,4=Fault...)
// // Không scale, trả thẳng integer
// static void get_charger_mode(void *ptr, uint8_t *len)
// {
//   psu_data_t psu = psu_data_read();
//   int32_t val = (int32_t)psu.charger_mode;
//   *(int32_t *)ptr = val;
//   *len = sizeof(val);
// }

// Cờ cảnh báo theo ngưỡng cấu hình (bitmask tự suy ra từ điện áp/SOC)
// bit0: SOC<=30%, bit1: SOC<=20%, bit2: SOC<=10%, bit3: V<=cutoff
static void get_batt_warn_flags(void *ptr, uint8_t *len)
{
  psu_data_t psu = psu_data_read();
  const app_config_t *cfg = app_cfg_get();
  uint32_t flags = 0;

  float soc = psu.batt_soc;
  if (soc <= 30.0f)
    flags |= (1u << 0);
  if (soc <= 20.0f)
    flags |= (1u << 1);
  if (soc <= 10.0f)
    flags |= (1u << 2);

  if (cfg)
  {
    float v = psu.batt_voltage;
    if (v <= cfg->psu.voltage_cutoff_low)
      flags |= (1u << 3);
  }

  *(uint32_t *)ptr = flags;
  *len = sizeof(flags);
}

static uint8_t ber_encode_base128(uint32_t v, uint8_t *out)
{
  uint8_t tmp[5];
  int n = 0;
  do
  {
    tmp[n++] = v & 0x7F;
    v >>= 7;
  } while (v);
  for (int i = n - 1, j = 0; i >= 0; --i, ++j)
    out[j] = tmp[i] | (i ? 0x80 : 0x00);
  return (uint8_t)n;
}
uint8_t write_enterprise_root(uint8_t *buf)
{
  /* 1.3.6.1.4.1.<ENTERPRISE_ID> */
  uint8_t *p = buf;
  *p++ = 0x2b;
  *p++ = 6;
  *p++ = 1;
  *p++ = 4;
  *p++ = 1;
  p += ber_encode_base128(ENTERPRISE_ID, p);
  return (uint8_t)(p - buf);
}


/* ===================================================================== */
/*                          MIB TABLE                                     */
/* ===================================================================== */

dataEntryType snmpData[] =
    {
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_input_voltage, NULL},  // 1 VIN (0.01V)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_output_voltage, NULL}, // 2 VOUT (0.01V)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_set_voltage, NULL},    // 3 VSET (0.01V)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_output_current, NULL}, // 4 IOUT (mA)
        // ===== Battery block =====
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_voltage, NULL},      // 10 VBAT (0.01V)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_soc, NULL},          // 11 SOC (0.1%)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_capacity_ah, NULL},  // 12 Capacity Ah (0.01Ah)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_remaining_ah, NULL}, // 13 Remaining Ah (0.01Ah)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_runtime_min, NULL},  // 14 Runtime (minutes)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_temp, NULL},         // 15 Batt temp (0.01°C)
        // {0,{0}, SNMPDTYPE_INTEGER, 4, {""}, get_charger_mode,      NULL}, // 16 Charger mode (enum)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_warn_flags, NULL}, // 17 Warn flags (bitmask)
};
const int32_t maxData = (int32_t)(sizeof(snmpData) / sizeof(snmpData[0]));

/* ============================= */
/*  Build OID for scalar object  */
/* ============================= */
static uint8_t build_oid_scalar(uint8_t *buf, uint8_t group, uint8_t leaf)
{
  uint8_t len = write_enterprise_root(buf);
  buf[len++] = group;
  buf[len++] = leaf;
  buf[len++] = 0; // instance .0
  return len;
}
void snmp_custom_init_oids(void)
{
  // ===== Measurements =====
  snmpData[0].oidlen = build_oid_scalar(snmpData[0].oid, GROUP_MEAS, LEAF_VIN);
  snmpData[1].oidlen = build_oid_scalar(snmpData[1].oid, GROUP_MEAS, LEAF_VOUT);
  snmpData[2].oidlen = build_oid_scalar(snmpData[2].oid, GROUP_MEAS, LEAF_VSET);
  snmpData[3].oidlen = build_oid_scalar(snmpData[3].oid, GROUP_MEAS, LEAF_IOUT);

  // ===== Battery =====
  snmpData[4].oidlen = build_oid_scalar(snmpData[4].oid, GROUP_BATT, LEAF_BAT_V);
  snmpData[5].oidlen = build_oid_scalar(snmpData[5].oid, GROUP_BATT, LEAF_BAT_SOC);
  snmpData[6].oidlen = build_oid_scalar(snmpData[6].oid, GROUP_BATT, LEAF_BAT_CAP);
  snmpData[7].oidlen = build_oid_scalar(snmpData[7].oid, GROUP_BATT, LEAF_BAT_REM);
  snmpData[8].oidlen = build_oid_scalar(snmpData[8].oid, GROUP_BATT, LEAF_BAT_RT);
  snmpData[9].oidlen = build_oid_scalar(snmpData[9].oid, GROUP_BATT, LEAF_BAT_TMP);

  // Nếu bật lại charger_mode:
  // snmpData[10].oidlen = build_oid_scalar(snmpData[10].oid, GROUP_BATT, LEAF_CHG_MD);

  // Warn flags (bitmask)
  // Nếu bạn đang dùng vị trí idx 10 cho Warn flags:
  snmpData[10].oidlen = build_oid_scalar(snmpData[10].oid, GROUP_BATT, LEAF_BAT_WRN);

  /* Severity (scalar) dùng cho trap */
  build_oid_scalar(OID_TRAP_SEVERITY, 1, 99);
}

__attribute__((constructor)) static void _snmp_custom_ctor(void) { snmp_custom_init_oids(); }


/* ===================================================================== */
/*                        OID UTILITIES (BER-128)                         */
/* ===================================================================== */

static uint8_t build_oid_branch(uint8_t *buf, uint8_t leaf)
{
  uint8_t len = write_enterprise_root(buf);
  buf[len++] = 1;    // group 1 (PSU basic)
  buf[len++] = leaf; // index
  return len;
}

uint8_t get_trap_severity(uint16_t trap_code)
{
  switch (trap_code)
  {
  case 100:
    return 3; // AC Loss
  case 101:
    return 4; // Restored
  case 103:
    return 2; // Battery <30min
  case 113:
    return 3; // HW Fault
  case 114:
    return 3; // BMS Fault
  case 117:
    return 1; // Battery Full
  default:
    return 2; // Default Warning
  }
}
// =======================
// Helper: tạo varbind INTEGER
// =======================
static dataEntryType makeIntVar(const uint8_t *oid, uint8_t oidlen, int32_t value)
{
  dataEntryType var = {0};

  var.oidlen = oidlen;
  memcpy(var.oid, oid, oidlen);

  var.dataType = SNMPDTYPE_INTEGER; // kiểu dữ liệu SNMP INTEGER
  var.dataLen = 4;                  // 4 byte
  var.u.intval = value;             // Big-endian để SNMP đọc đúng

  var.getfunction = NULL;
  var.setfunction = NULL;

  return var;
}

/* ===================================================================== */
/*                         TRAP SEND WRAPPER                              */
/* ===================================================================== */
void snmp_send_trap_custom(uint8_t *managerIP, uint8_t *agentIP, uint8_t trap_code)
{
  (void)agentIP;

  uint8_t ev_oid[16];
  uint8_t len = write_enterprise_root(ev_oid); // 1.3.6.1.4.1.<EID>
  ev_oid[len++] = 10;
  ev_oid[len++] = trap_code;

  // VarBind: trapSeverity
  dataEntryType vars[1];
  vars[0] = makeIntVar(OID_TRAP_SEVERITY, sizeof(OID_TRAP_SEVERITY), get_trap_severity(trap_code));

  snmp_sendTrapV2(managerIP, (int8_t *)COMMUNITY, ev_oid, len, vars, 1);
}

/* ===================================================================== */
/*                        WARM START TRAP                                 */
/* ===================================================================== */
void initial_Trap(uint8_t *managerIP, uint8_t *agentIP)
{
  dataEntryType enterprise_oid = {0};
  enterprise_oid.oidlen = write_enterprise_root(enterprise_oid.oid);
  uint8_t warmStartOID[] = {1, 3, 6, 1, 6, 3, 1, 1, 5, 2};
  snmp_sendTrapV2(managerIP, (int8_t *)COMMUNITY, warmStartOID, sizeof(warmStartOID), NULL, 0);

  // snmp_sendTrap(managerIP, agentIP, (int8_t*)COMMUNITY,
  //               enterprise_oid, SNMPTRAP_WARMSTART, 0 /* specific */, 0);
}

/* ===================================================================== */
/*                 TRAP: FAULT CHANGE (AC/OP_OFF/OTP/OLP)                 */
/* ===================================================================== */
static uint16_t last_fault = 0xFFFF;
void snmp_trap_state_reset(void) { last_fault = 0xFFFF; }

void snmp_process_fault_trap(uint8_t *managerIP, uint8_t *agentIP)
{
    static uint16_t last_fault = 0xFFFF;   // 0xFFFF = chưa khởi tạo

    // Đọc trạng thái PSU hiện tại (thread-safe)
    psu_data_t psu = psu_data_read();
    uint16_t new_fault = psu.fault.raw;

    // Lần đầu: chỉ ghi nhận trạng thái ban đầu để tránh bắn trap giả
    if (last_fault == 0xFFFF) {
        last_fault = new_fault;
        return;
    }

    uint16_t changed = (uint16_t)(last_fault ^ new_fault);
    if (!changed) return;  // không có thay đổi -> thoát nhanh

    const app_config_t *cfg = app_cfg_get();
    uint8_t mask = cfg ? cfg->trap_enable_mask : 0xFF; // nếu chưa có cfg thì bắn tất cả

    printf("[SNMP] Fault changed: old=0x%04X new=0x%04X (mask=0x%02X)\n",
           last_fault, new_fault, mask);

    // ===== AC_FAIL: Power Failure / Power Restored =====
    if (changed & (1u << BIT_ACFAIL)) {
        if (TRAP_EN_POWERFAIL(mask)) {
            if (psu.fault.bits.ac_fail)
                snmp_send_trap_custom(managerIP, agentIP, TRAP_POWER_FAILURE);
            else
                snmp_send_trap_custom(managerIP, agentIP, TRAP_POWER_RESTORED);
        }
    }

    // ===== OP_OFF: Output Disabled / Output Restored =====
    if (changed & (1u << BIT_OPOFF)) {
        // tuỳ chọn: dùng chung bit enable với PowerFail, hoặc luôn bắn
        if (TRAP_EN_POWERFAIL(mask)) {
            if (psu.fault.bits.op_off)
                snmp_send_trap_custom(managerIP, agentIP, TRAP_OUTPUT_DISABLED);
            else
                snmp_send_trap_custom(managerIP, agentIP, TRAP_OUTPUT_RESTORED);
        }
    }

    // ===== OTP: Over Temperature Set/Cleared =====
    if (changed & (1u << BIT_OTP)) {
        if (TRAP_EN_OVERTEMP(mask)) {
            if (psu.fault.bits.otp)
                snmp_send_trap_custom(managerIP, agentIP, TRAP_OVER_TEMPERATURE);
            else
                snmp_send_trap_custom(managerIP, agentIP, TRAP_OTP_CLEARED);
        }
    }

    // ===== OLP: Over Current Set/Cleared =====
    if (changed & (1u << BIT_OLP)) {
        if (TRAP_EN_OVERCURR(mask)) {
            if (psu.fault.bits.olp)
                snmp_send_trap_custom(managerIP, agentIP, TRAP_OVER_CURRENT);
            else
                snmp_send_trap_custom(managerIP, agentIP, TRAP_OCP_CLEARED);
        }
    }

    // (Nếu bạn muốn thêm OVP, SHORT, FAN_FAIL … thì lặp lại mẫu trên)

    last_fault = new_fault;
}

/* ===================================================================== */
/*            TRAP: SPEC 5.1–5.2 (runtime / SOC / temp / misc)           */
/* ===================================================================== */

/* Tick ms (dựa trên SNMP time tick 10ms trong snmp.c) */
static inline uint32_t now_ms_for_trap(void)
{
  extern uint32_t getSNMPTimeTick(void);
  return getSNMPTimeTick() * 10U;
}
#define MAX_TRAP_CODE 512
static uint32_t g_last_sent_ms[MAX_TRAP_CODE];

static inline uint8_t can_send_es(uint16_t code, uint32_t tnow)
{
  if (code >= MAX_TRAP_CODE)
    return 0;
  if (tnow - g_last_sent_ms[code] < TRAP_RATELIMIT_MS)
    return 0;
  g_last_sent_ms[code] = tnow;
  return 1;
}
static inline void send_es(uint8_t *m, uint8_t *a, uint16_t code)
{
  uint32_t tnow = now_ms_for_trap();
  if (can_send_es(code, tnow))
    snmp_send_trap_custom(m, a, (uint8_t)code);
}

/* Debounce cho 1 ngưỡng */
typedef struct
{
  uint8_t latched, cnt_set, cnt_clr;
} thr_t;
static thr_t thr_run60, thr_run30, thr_run15, thr_run5;
static thr_t thr_soc30, thr_soc20, thr_soc10;
static thr_t thr_tHigh, thr_tCrit;

/* Trạng thái đơn */
static uint8_t st_ac_loss = 0, st_overload = 0, st_hw_fault = 0, st_bms_fault = 0, st_emergency = 0, st_full_charge = 0;

static inline uint8_t debounce(thr_t *t, uint8_t set_cond, uint8_t clr_cond)
{
  if (!t->latched)
  {
    if (set_cond)
    {
      if (t->cnt_set < TRAP_DEBOUNCE_SAMPLES)
        t->cnt_set++;
      t->cnt_clr = 0;
      if (t->cnt_set >= TRAP_DEBOUNCE_SAMPLES)
      {
        t->cnt_set = 0;
        t->latched = 1;
        return 1;
      }
    }
    else
    {
      t->cnt_set = 0;
      t->cnt_clr = 0;
    }
  }
  else
  {
    if (clr_cond)
    {
      if (t->cnt_clr < TRAP_DEBOUNCE_SAMPLES)
        t->cnt_clr++;
      t->cnt_set = 0;
      if (t->cnt_clr >= TRAP_DEBOUNCE_SAMPLES)
      {
        t->cnt_clr = 0;
        t->latched = 0;
        return 2;
      }
    }
    else
    {
      t->cnt_clr = 0;
      t->cnt_set = 0;
    }
  }
  return 0;
}

void snmp_trap_policy_reset(void)
{
  memset(&thr_run60, 0, sizeof(thr_run60));
  memset(&thr_run30, 0, sizeof(thr_run30));
  memset(&thr_run15, 0, sizeof(thr_run15));
  memset(&thr_run5, 0, sizeof(thr_run5));
  memset(&thr_soc30, 0, sizeof(thr_soc30));
  memset(&thr_soc20, 0, sizeof(thr_soc20));
  memset(&thr_soc10, 0, sizeof(thr_soc10));
  memset(&thr_tHigh, 0, sizeof(thr_tHigh));
  memset(&thr_tCrit, 0, sizeof(thr_tCrit));
  st_ac_loss = st_overload = st_hw_fault = st_bms_fault = st_emergency = st_full_charge = 0;
  memset(g_last_sent_ms, 0, sizeof(g_last_sent_ms));
}

/* Mọi thứ an toàn? -> bắn SystemNormal */
static inline uint8_t is_all_safe(uint8_t ac_present,
                                  float batt_v, float soc_pct, float runtime_min, float temp_c,
                                  uint8_t overload, uint8_t hw_fault, uint8_t bms_fault)
{
  uint8_t safe = 1;
  if (!ac_present)
    safe = 0;
  if (temp_c > (TEMP_HIGH_C - 2.0f))
    safe = 0; // về < 48°C
  if (overload)
    safe = 0;
  if (hw_fault || bms_fault)
    safe = 0;
  if (batt_v < (VBAT_EMERGENCY_V + 0.5f))
    safe = 0; // >48.5V
  if (soc_pct < (30.0f + SOC_HYS_PCT))
    safe = 0; // >32%
  if (runtime_min < (60.0f + RUNTIME_HYS_MIN))
    safe = 0; // >62’
  return safe;
}

void snmp_trap_process_spec(uint8_t *managerIP, uint8_t *agentIP,
                            uint8_t ac_present,
                            float batt_v, float soc_pct, float runtime_min, float temp_c,
                            uint8_t overload, uint8_t hw_fault, uint8_t bms_fault)
{
  /* 0) Fully charged (Informational, 1 lần khi ~100%) */
  if (ac_present && soc_pct >= 99.0f && !st_full_charge)
  {
    send_es(managerIP, agentIP, ES_TRAP_BATTERY_FULLY_CHARGED);
    st_full_charge = 1;
  }
  if (!ac_present || soc_pct < 95.0f)
    st_full_charge = 0;

  /* 1) AC Loss / Restored */
  if (!ac_present && !st_ac_loss)
  {
    send_es(managerIP, agentIP, ES_TRAP_AC_POWER_LOSS);
    st_ac_loss = 1;
    /* reset latch runtime khi vừa mất AC */
    thr_run60.latched = thr_run30.latched = thr_run15.latched = thr_run5.latched = 0;
    thr_run60.cnt_set = thr_run30.cnt_set = thr_run15.cnt_set = thr_run5.cnt_set = 0;
    thr_run60.cnt_clr = thr_run30.cnt_clr = thr_run15.cnt_clr = thr_run5.cnt_clr = 0;
  }
  if (ac_present && st_ac_loss)
  {
    send_es(managerIP, agentIP, ES_TRAP_AC_POWER_RESTORED);
    st_ac_loss = 0;
  }

  /* 2) Temperature */
  uint8_t e = debounce(&thr_tHigh, (temp_c > TEMP_HIGH_C), (temp_c < (TEMP_HIGH_C - 2.0f)));
  if (e == 1)
    send_es(managerIP, agentIP, ES_TRAP_TEMPERATURE_HIGH);

  e = debounce(&thr_tCrit, (temp_c > TEMP_CRIT_C), (temp_c < (TEMP_CRIT_C - 2.0f)));
  if (e == 1)
    send_es(managerIP, agentIP, ES_TRAP_TEMPERATURE_CRITICAL);

  /* 3) Overload */
  if (overload && !st_overload)
  {
    send_es(managerIP, agentIP, ES_TRAP_OVERLOAD_CONDITION);
    st_overload = 1;
  }
  if (!overload && st_overload)
  {
    st_overload = 0;
  }

  /* 4) Runtime thresholds – on-battery */
  if (!ac_present)
  {
    e = debounce(&thr_run5, (runtime_min <= 5.0f), (runtime_min >= (5.0f + RUNTIME_HYS_MIN)));
    if (e == 1)
      send_es(managerIP, agentIP, ES_TRAP_RUNTIME_5MIN);
    e = debounce(&thr_run15, (runtime_min <= 15.0f), (runtime_min >= (15.0f + RUNTIME_HYS_MIN)));
    if (e == 1 && !thr_run5.latched)
      send_es(managerIP, agentIP, ES_TRAP_RUNTIME_15MIN);
    e = debounce(&thr_run30, (runtime_min <= 30.0f), (runtime_min >= (30.0f + RUNTIME_HYS_MIN)));
    if (e == 1 && !thr_run15.latched && !thr_run5.latched)
      send_es(managerIP, agentIP, ES_TRAP_RUNTIME_30MIN);
    e = debounce(&thr_run60, (runtime_min <= 60.0f), (runtime_min >= (60.0f + RUNTIME_HYS_MIN)));
    if (e == 1 && !thr_run30.latched && !thr_run15.latched && !thr_run5.latched)
      send_es(managerIP, agentIP, ES_TRAP_RUNTIME_60MIN);
  }

  /* 5) SOC thresholds – ưu tiên mốc thấp hơn */
  e = debounce(&thr_soc10, (soc_pct <= 10.0f), (soc_pct >= (10.0f + SOC_HYS_PCT)));
  if (e == 1)
    send_es(managerIP, agentIP, ES_TRAP_SOC_10PCT);
  e = debounce(&thr_soc20, (soc_pct <= 20.0f), (soc_pct >= (20.0f + SOC_HYS_PCT)));
  if (e == 1 && !thr_soc10.latched)
    send_es(managerIP, agentIP, ES_TRAP_SOC_20PCT);
  e = debounce(&thr_soc30, (soc_pct <= 30.0f), (soc_pct >= (30.0f + SOC_HYS_PCT)));
  if (e == 1 && !thr_soc20.latched && !thr_soc10.latched)
    send_es(managerIP, agentIP, ES_TRAP_SOC_30PCT);

  /* 6) Emergency */
  if ((batt_v > 0.1f && batt_v < VBAT_EMERGENCY_V) || soc_pct <= 10.0f)
  {
    if (!st_emergency)
    {
      send_es(managerIP, agentIP, ES_TRAP_BATTERY_EMERGENCY_SHUTDOWN);
      st_emergency = 1;
    }
  }
  else if (st_emergency && (batt_v > VBAT_EMERGENCY_V + 0.5f) && soc_pct > 12.0f)
  {
    st_emergency = 0;
  }

  if (hw_fault && !st_hw_fault)
  {
    send_es(managerIP, agentIP, ES_TRAP_HARDWARE_FAULT);
    st_hw_fault = 1;
  }
  if (!hw_fault && st_hw_fault)
  {
    st_hw_fault = 0;
  }
  if (bms_fault && !st_bms_fault)
  {
    send_es(managerIP, agentIP, ES_TRAP_BMS_FAULT);
    st_bms_fault = 1;
  }
  if (!bms_fault && st_bms_fault)
  {
    st_bms_fault = 0;
  }
  /* Nếu có cờ shutdown nhiệt riêng, bắn ES_TRAP_OVERTEMP_SHUTDOWN ở đây. */

  /* 7) System Normal */
  static uint8_t sent_normal = 0;
  if (is_all_safe(ac_present, batt_v, soc_pct, runtime_min, temp_c, overload, hw_fault, bms_fault))
  {
    if (!sent_normal)
    {
      send_es(managerIP, agentIP, ES_TRAP_SYSTEM_NORMAL);
      sent_normal = 1;
    }
  }
  else
  {
    sent_normal = 0;
  }
}
