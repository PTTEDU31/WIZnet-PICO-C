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
// ==== Group 10: TRAP ====
#define GROUP_TRAP 10

static uint8_t OID_TRAP_SITE_ID[16];
// uint8_t OID_TRAP_CODE[] = {1, 3, 6, 1, 4, 1, 99999, 10, 2, 0};
// uint8_t OID_TRAP_MESSAGE[] = {1, 3, 6, 1, 4, 1, 99999, 10, 3, 0};
// uint8_t OID_TRAP_TIMESTAMP[] = {1, 3, 6, 1, 4, 1, 99999, 10, 4, 0};
// uint8_t OID_TRAP_SITE_ID[] = {1, 3, 6, 1, 4, 1, 99999, 10, 5, 0};
// uint8_t OID_TRAP_BATT_VOLT[] = {1, 3, 6, 1, 4, 1, 99999, 10, 6, 0};
// uint8_t OID_TRAP_BATT_SOC[] = {1, 3, 6, 1, 4, 1, 99999, 10, 7, 0};
// uint8_t OID_TRAP_BATT_RUNTIME[] = {1, 3, 6, 1, 4, 1, 99999, 10, 8, 0};
// uint8_t OID_TRAP_TEMP[] = {1, 3, 6, 1, 4, 1, 99999, 10, 9, 0};
// uint8_t OID_TRAP_AC_STATUS[] = {1, 3, 6, 1, 4, 1, 99999, 10, 10, 0};

#define TRAP_EN_POWERFAIL(mask) ((mask) & (1u << 0))
#define TRAP_EN_OVERTEMP(mask) ((mask) & (1u << 1))
#define TRAP_EN_OVERCURR(mask) ((mask) & (1u << 2))
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
static void get_ac_fault_flags(void *ptr, uint8_t *len)
{
  psu_data_t psu = psu_data_read();
  uint32_t val = 0;
  // Giả sử bit0: AC mất
  if (psu.fault.bits.ac_fail)
    val |= (1u << 0);
  *(uint32_t *)ptr = val;
  *len = sizeof(val);
}

static void get_ac_loss_time(void *ptr, uint8_t *len)
{
  // todo:  Hiện chưa có dữ liệu thời gian mất AC, trả 0 tạm
  uint32_t val = 0;
  *(uint32_t *)ptr = val;
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
static void get_charger_mode(void *ptr, uint8_t *len)
{
  psu_data_t psu = psu_data_read();
  int32_t val = (int32_t)psu.charger_mode;
  *(int32_t *)ptr = val;
  *len = sizeof(val);
}

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

static void get_system_runtime_min(void *ptr, uint8_t *len)
{
  // Trả thời gian hệ thống chạy (uptime) tính bằng phút
  // Giả sử hàm system_uptime_minutes() có sẵn
  uint32_t val = (uint32_t)to_ms_since_boot(get_absolute_time());
  val /= 60000; // chuyển ms → phút
  *(uint32_t *)ptr = val;
  *len = sizeof(val);
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
        // ===== AC Power block =====
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_input_voltage, NULL},  // 0 ACVIN (0.01V)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_ac_fault_flags, NULL}, // 1 AC_STATUS (enum)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_ac_loss_time, NULL},   // 2 AC_LOSS_TIME (s)

        // ===== Battery block =====
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_voltage, NULL},      // 3 VBAT (0.01V)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_capacity_ah, NULL},  // 4 Capacity Ah (0.01Ah)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_soc, NULL},          // 5 SOC (0.1%)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_remaining_ah, NULL}, // 6 Remaining Ah (0.01Ah)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_runtime_min, NULL},  // 7 Runtime (minutes)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_temp, NULL},         // 8 Batt temp (0.01°C)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_charger_mode, NULL},      // 9 Charger mode (enum)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_batt_warn_flags, NULL},   // 10 Warn flags (bitmask)

        // ===== Output block =====
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_output_voltage, NULL}, // 11 OUT_V (0.01V)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_output_current, NULL}, // 12 OUT_I (mA)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_set_voltage, NULL},    // 13 OUT_P (W)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_fault_flags, NULL},    // 14 OUT_S (enum)

        // ===== Environment block =====
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_temp, NULL}, // 15 ENV_TEMP (0.01°C)
                                                              // {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_humidity,          NULL}, // 16 ENV_HUMI (0.1%RH)

        // ===== System block =====
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_system_runtime_min, NULL}, // 17 SYS_UPTIME (minutes)
        {0, {0}, SNMPDTYPE_INTEGER, 4, {""}, get_fault_flags, NULL},        // 18 SYS_HWFAULT (bitmask)
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
static uint8_t build_oid_branch(uint8_t *buf, uint8_t leaf)
{
  uint8_t len = write_enterprise_root(buf);
  buf[len++] = 1;    // group 1 (PSU basic)
  buf[len++] = leaf; // index
  return len;
}

void snmp_custom_init_oids(void)
{
  // ===== Measurements (AC Power) =====
  snmpData[0].oidlen = build_oid_scalar(snmpData[0].oid, GROUP_AC_POWER, LEAF_ACVIN);
  snmpData[1].oidlen = build_oid_scalar(snmpData[1].oid, GROUP_AC_POWER, LEAF_AC_STATUS);
  snmpData[2].oidlen = build_oid_scalar(snmpData[2].oid, GROUP_AC_POWER, LEAF_AC_LOSS_TIME);

  // // ===== Battery =====
  snmpData[3].oidlen = build_oid_scalar(snmpData[3].oid, GROUP_BATT, LEAF_BAT_V);
  snmpData[4].oidlen = build_oid_scalar(snmpData[4].oid, GROUP_BATT, LEAF_BAT_CAP);
  snmpData[5].oidlen = build_oid_scalar(snmpData[5].oid, GROUP_BATT, LEAF_BAT_SOC);
  snmpData[6].oidlen = build_oid_scalar(snmpData[6].oid, GROUP_BATT, LEAF_BAT_REM);
  snmpData[7].oidlen = build_oid_scalar(snmpData[7].oid, GROUP_BATT, LEAF_BAT_RT);
  snmpData[8].oidlen = build_oid_scalar(snmpData[8].oid, GROUP_BATT, LEAF_BAT_TMP);
  snmpData[9].oidlen = build_oid_scalar(snmpData[9].oid, GROUP_BATT, LEAF_CHG_MD);
  snmpData[10].oidlen = build_oid_scalar(snmpData[10].oid, GROUP_BATT, LEAF_BAT_WRN);

  // // ===== Output =====
  snmpData[11].oidlen = build_oid_scalar(snmpData[11].oid, GROUP_OUTPUT, LEAF_OUT_V);
  snmpData[12].oidlen = build_oid_scalar(snmpData[12].oid, GROUP_OUTPUT, LEAF_OUT_I);
  snmpData[13].oidlen = build_oid_scalar(snmpData[13].oid, GROUP_OUTPUT, LEAF_OUT_P);
  snmpData[14].oidlen = build_oid_scalar(snmpData[14].oid, GROUP_OUTPUT, LEAF_OUT_S);

  // // // ===== Environment =====
  snmpData[15].oidlen = build_oid_scalar(snmpData[15].oid, GROUP_ENV, LEAF_ENV_TEMP);
  // // snmpData[16].oidlen = build_oid_scalar(snmpData[16].oid, GROUP_ENV, LEAF_ENV_HUMI);

  // // // ===== System =====
  snmpData[16].oidlen = build_oid_scalar(snmpData[16].oid, GROUP_SYSTEM, LEAF_SYS_UPTIME);
  snmpData[17].oidlen = build_oid_scalar(snmpData[17].oid, GROUP_SYSTEM, LEAF_SYS_HWFAULT);

  // ===== Trap severity (scalar) =====
  build_oid_scalar(OID_TRAP_SEVERITY, 1, 99);
  build_oid_scalar(OID_TRAP_SITE_ID, 1,55);
}

__attribute__((constructor)) static void _snmp_custom_ctor(void) { snmp_custom_init_oids(); }

/* ===================================================================== */
/*                        OID UTILITIES (BER-128)                         */
/* ===================================================================== */


uint8_t get_trap_severity(uint16_t trap_code)
{
  switch (trap_code)
  {
  /* ===== Severity 1 – Informational ===== */
  case ES_TRAP_AC_POWER_RESTORED:
  case ES_TRAP_BATTERY_FULLY_CHARGED:
  case ES_TRAP_SYSTEM_NORMAL:
    return 1;

  /* ===== Severity 2 – Warning ===== */
  case ES_TRAP_AC_POWER_LOSS:
  case ES_TRAP_RUNTIME_60MIN:
  case ES_TRAP_RUNTIME_30MIN:
  case ES_TRAP_TEMPERATURE_HIGH:
  case ES_TRAP_SOC_30PCT:
    return 2;

  /* ===== Severity 3 – Critical ===== */
  case ES_TRAP_RUNTIME_15MIN:
  case ES_TRAP_RUNTIME_5MIN:
  case ES_TRAP_SOC_20PCT:
  case ES_TRAP_SOC_10PCT:
  case ES_TRAP_TEMPERATURE_CRITICAL:
  case ES_TRAP_OVERLOAD_CONDITION:
    return 3;

  /* ===== Severity 4 – Emergency ===== */
  case ES_TRAP_BATTERY_EMERGENCY_SHUTDOWN:
  case ES_TRAP_HARDWARE_FAULT:
  case ES_TRAP_OVERTEMP_SHUTDOWN:
  case ES_TRAP_BMS_FAULT:
    return 4;

  /* ===== Unknown or unspecified traps ===== */
  default:
    return 2; // Default to "Warning"
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

dataEntryType makeStrVar(const uint8_t *oid, uint8_t oid_len, const char *str)
{
    dataEntryType var = {0};

    // Sao chép OID
    var.oidlen = oid_len;
    memcpy(var.oid, oid, oid_len);

    // Gán kiểu dữ liệu và chiều dài
    var.dataType = SNMPDTYPE_OCTET_STRING;

    size_t len = strlen(str);
    if (len >= MAX_STRING)
        len = MAX_STRING - 1;  // tránh tràn bộ đệm

    memcpy(var.u.octetstring, str, len);
    var.dataLen = (uint8_t)len;

    // Không có hàm get/set cho trap
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

  psu_data_t psu = psu_data_read();
  app_config_t const *cfg = app_cfg_get();

  // ===== VarBinds =====
  dataEntryType vars[6];
  uint8_t vcount = 0;

  // 1️⃣ Mức độ nghiêm trọng
  vars[vcount++] = makeIntVar(OID_TRAP_SEVERITY, sizeof(OID_TRAP_SEVERITY),
                              get_trap_severity(trap_code));

  // 2️⃣ Thêm các thông số ngữ cảnh về pin / hệ thống
  // vars[vcount++] = makeIntVar(OID_TRAP_BATT_VOLT, sizeof(OID_TRAP_BATT_VOLT), 123);
  // vars[vcount++] = makeIntVar(OID_TRAP_BATT_SOC, sizeof(OID_TRAP_BATT_SOC), 234);
  // vars[vcount++] = makeIntVar(OID_TRAP_BATT_SOC, sizeof(OID_TRAP_BATT_SOC), 222);
  // vars[vcount++] = makeIntVar(OID_TRAP_BATT_SOC, sizeof(OID_TRAP_BATT_SOC), 444);

  // vars[vcount++] = makeStrVar(OID_TRAP_SITE_ID, sizeof(OID_TRAP_SITE_ID), "cfg->device_name");
  vars[vcount++] = makeIntVar(OID_TRAP_SITE_ID, sizeof(OID_TRAP_SITE_ID), (int)psu.batt_voltage*100);

  printf("[SNMP] Trap %u sent with %u varbinds\n", trap_code, vcount);

  // Gửi trap
  snmp_sendTrapV2(managerIP, (int8_t *)COMMUNITY, ev_oid, len, vars, vcount);
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
  static uint16_t last_fault = 0xFFFF;      // 0xFFFF = chưa khởi tạo
  static uint8_t last_runtime_level = 0xFF; // theo dõi mức cảnh báo runtime trước đó
  static uint8_t last_soc_level = 0xFF;     // theo dõi mức cảnh báo SOC trước đó

  // Đọc trạng thái PSU hiện tại (thread-safe)
  psu_data_t psu = psu_data_read();
  uint16_t new_fault = psu.fault.raw;

  // Lần đầu: chỉ ghi nhận trạng thái ban đầu để tránh bắn trap giả
  // Lần đầu tiên khởi tạo
  if (last_fault == 0xFFFF)
  {
    last_fault = new_fault;
    last_runtime_level = 0xFF;
    last_soc_level = 0xFF;
    return;
  }
  uint16_t changed = (uint16_t)(last_fault ^ new_fault);
  if (!changed)
    return; // không có thay đổi -> thoát nhanh

  const app_config_t *cfg = app_cfg_get();
  uint8_t mask = cfg ? cfg->trap_enable_mask : 0xFF; // nếu chưa có cfg thì bắn tất cả

  printf("[SNMP] Fault changed: old=0x%04X new=0x%04X (mask=0x%02X)\n",
         last_fault, new_fault, mask);

  // ===== AC_FAIL: Power Failure / Power Restored =====
  if (changed & (1u << BIT_ACFAIL))
  {
    if (TRAP_EN_POWERFAIL(mask))
    {
      if (psu.fault.bits.ac_fail)
        snmp_send_trap_custom(managerIP, agentIP, TRAP_POWER_FAILURE);
      else
        snmp_send_trap_custom(managerIP, agentIP, TRAP_POWER_RESTORED);
    }
  }

  // ===== OP_OFF: Output Disabled / Output Restored =====
  if (changed & (1u << BIT_OPOFF))
  {
    // tuỳ chọn: dùng chung bit enable với PowerFail, hoặc luôn bắn
    if (TRAP_EN_POWERFAIL(mask))
    {
      if (psu.fault.bits.op_off)
        snmp_send_trap_custom(managerIP, agentIP, TRAP_OUTPUT_DISABLED);
      else
        snmp_send_trap_custom(managerIP, agentIP, TRAP_OUTPUT_RESTORED);
    }
  }

  // ===== OTP: Over Temperature Set/Cleared =====
  if (changed & (1u << BIT_OTP))
  {
    if (TRAP_EN_OVERTEMP(mask))
    {
      if (psu.fault.bits.otp)
        snmp_send_trap_custom(managerIP, agentIP, TRAP_OVER_TEMPERATURE);
      else
        snmp_send_trap_custom(managerIP, agentIP, TRAP_OTP_CLEARED);
    }
  }

  // ===== OLP: Over Current Set/Cleared =====
  if (changed & (1u << BIT_OLP))
  {
    if (TRAP_EN_OVERCURR(mask))
    {
      if (psu.fault.bits.olp)
        snmp_send_trap_custom(managerIP, agentIP, TRAP_OVER_CURRENT);
      else
        snmp_send_trap_custom(managerIP, agentIP, TRAP_OCP_CLEARED);
    }
  }

  // ============================
  // ===== Battery runtime ======
  // ============================

  uint32_t runtime_min = psu.batt_runtime; // thời gian còn lại (phút)
  uint8_t runtime_level = 0xFF;

  const psu_config_t batt_cfg = cfg->psu;

  if (batt_cfg.warning_60min_enabled && runtime_min <= 60 && runtime_min > 30)
    runtime_level = 60;
  else if (batt_cfg.warning_30min_enabled && runtime_min <= 30 && runtime_min > 15)
    runtime_level = 30;
  else if (batt_cfg.warning_15min_enabled && runtime_min <= 15 && runtime_min > 5)
    runtime_level = 15;
  else if (batt_cfg.warning_5min_enabled && runtime_min <= 5)
    runtime_level = 5;

  if (runtime_level != 0xFF && runtime_level != last_runtime_level)
  {
    printf("[SNMP] Battery runtime warning: %u min remaining\n", runtime_min);
    switch (runtime_level)
    {
    case 60:
      snmp_send_trap_custom(managerIP, agentIP, ES_TRAP_RUNTIME_60MIN);
      break;
    case 30:
      snmp_send_trap_custom(managerIP, agentIP, ES_TRAP_RUNTIME_30MIN);
      break;
    case 15:
      snmp_send_trap_custom(managerIP, agentIP, ES_TRAP_RUNTIME_15MIN);
      break;
    case 5:
      snmp_send_trap_custom(managerIP, agentIP, ES_TRAP_RUNTIME_5MIN);
      break;
    }
    last_runtime_level = runtime_level;
  }

  // ============================
  // ===== Battery SOC traps ====
  // ============================

  float soc = psu.batt_soc; // phần trăm (0–100)
  uint8_t soc_level = 0xFF;

  if (cfg)
  {
    if (batt_cfg.warning_soc30_enabled && soc <= 30 && soc > 20)
      soc_level = 30;
    else if (batt_cfg.warning_soc20_enabled && soc <= 20 && soc > 10)
      soc_level = 20;
    else if (batt_cfg.warning_soc10_enabled && soc <= 10)
      soc_level = 10;
  }

  if (soc_level != 0xFF && soc_level != last_soc_level)
  {
    printf("[SNMP] Battery SOC warning: %.1f%%\n", soc);
    switch (soc_level)
    {
    case 30:
      snmp_send_trap_custom(managerIP, agentIP, ES_TRAP_SOC_30PCT);
      break;
    case 20:
      snmp_send_trap_custom(managerIP, agentIP, ES_TRAP_SOC_20PCT);
      break;
    case 10:
      snmp_send_trap_custom(managerIP, agentIP, ES_TRAP_SOC_10PCT);
      break;
    }
    last_soc_level = soc_level;
  }

  last_fault = new_fault;
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
