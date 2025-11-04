#ifndef _SNMP_CUSTOM_H_
#define _SNMP_CUSTOM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "snmp.h"
#include "dev_modbus.h"     // Để lấy g_vin, g_vout, g_vset, g_cc, fault_flags_t

/* Enterprise + Community */
#define ENTERPRISE_ID   99999
#define COMMUNITY       "public"
#define COMMUNITY_SIZE  (strlen(COMMUNITY))


// OID layout: 1.3.6.1.4.1.<enterprise>.1.<GROUP>.<LEAF>.0
// Gợi ý nhóm:
//  - GROUP_MEAS = 1: các đo lường PSU (VIN/VOUT/VSET/IOUT)
//  - GROUP_BATT = 2: các thông số Battery (VBAT/SOC/Capacity/Remain/Runtime/Temp/WarnFlags)

#define GROUP_MEAS   1
#define GROUP_BATT   2

// ===== MEAS leaves =====
#define LEAF_VIN     1
#define LEAF_VOUT    2
#define LEAF_VSET    3
#define LEAF_IOUT    4

// ===== BATT leaves =====
#define LEAF_BAT_V   1   // VBAT (0.01V)
#define LEAF_BAT_SOC 2   // SOC (0.1%)
#define LEAF_BAT_CAP 3   // Capacity Ah (0.01Ah)
#define LEAF_BAT_REM 4   // Remaining Ah (0.01Ah)
#define LEAF_BAT_RT  5   // Runtime (minutes)
#define LEAF_BAT_TMP 6   // Battery Temp (0.01°C)
// #define LEAF_CHG_MD  7 // Charger mode (enum) — nếu bật lại
#define LEAF_BAT_WRN 8   // Warn flags (bitmask)


// /* ===========================
//  * Global monitored variables
//  * =========================== */
// extern float g_vin;   // input voltage
// extern float g_vout;  // output voltage
// extern float g_vset;  // set voltage
// extern float g_iout;    // current output
// extern float g_cc;    // current limit
// extern float g_temp;    // current limit
// extern fault_flags_t g_fault_flags;   // trạng thái lỗi mới nhất
// extern uint16_t g_fault_status;

/* ======= BIT LỖI (nếu layout khác, đổi tại đây) ======= */
enum {
  BIT_OTP    = 1,
  BIT_OLP    = 3,
  BIT_ACFAIL = 5,
  BIT_OPOFF  = 6,
};

/* ======= Trap cổ điển (enterpriseSpecific) cho fault thay đổi ======= */
typedef enum {
  TRAP_POWER_FAILURE        = 1,
  TRAP_POWER_RESTORED       = 2,
  TRAP_OVER_TEMPERATURE     = 3,
  TRAP_OVER_CURRENT         = 4,
  TRAP_OUTPUT_DISABLED      = 5,
  TRAP_OUTPUT_RESTORED      = 6,

  TRAP_OTP_CLEARED          = 100,
  TRAP_OCP_CLEARED          = 101
} trap_code_t;

/* ======= Trap theo SPEC 5.1–5.2 (specificTrap mã rõ ràng) ======= */
typedef enum {
  /* Severity 1 – Informational */
  ES_TRAP_AC_POWER_RESTORED         = 101,
  ES_TRAP_BATTERY_FULLY_CHARGED     = 102,
  ES_TRAP_SYSTEM_NORMAL             = 103,

  /* Severity 2 – Warning */
  ES_TRAP_AC_POWER_LOSS             = 201,
  ES_TRAP_RUNTIME_60MIN             = 202,
  ES_TRAP_RUNTIME_30MIN             = 203,
  ES_TRAP_TEMPERATURE_HIGH          = 204, // >50 °C
  ES_TRAP_SOC_30PCT                 = 205, // 30%

  /* Severity 3 – Critical */
  ES_TRAP_RUNTIME_15MIN             = 301,
  ES_TRAP_RUNTIME_5MIN              = 302,
  ES_TRAP_SOC_20PCT                 = 303,
  ES_TRAP_SOC_10PCT                 = 304,
  ES_TRAP_TEMPERATURE_CRITICAL      = 305, // >60 °C
  ES_TRAP_OVERLOAD_CONDITION        = 306,

  /* Severity 4 – Emergency */
  ES_TRAP_BATTERY_EMERGENCY_SHUTDOWN= 401,
  ES_TRAP_HARDWARE_FAULT            = 402,
  ES_TRAP_OVERTEMP_SHUTDOWN         = 403,
  ES_TRAP_BMS_FAULT                 = 404
} es_trap_code_t;

/* ======= Hysteresis / debounce / rate-limit ======= */
#define TRAP_DEBOUNCE_SAMPLES   5
#define TRAP_RATELIMIT_MS       00000UL // for test

#define RUNTIME_HYS_MIN         2.0f
#define SOC_HYS_PCT             2.0f
#define TEMP_HIGH_C             50.0f
#define TEMP_CRIT_C             60.0f
#define VBAT_EMERGENCY_V        48.0f

/* SNMP Data Table */
extern dataEntryType snmpData[];
extern const int32_t maxData;

/* ======= OID init + WarmStart + gửi trap ======= */
void snmp_custom_init_oids(void);
void initial_Trap(uint8_t *managerIP, uint8_t *agentIP);
void snmp_send_trap_custom(uint8_t *managerIP, uint8_t *agentIP, uint8_t trap_code);
uint8_t write_enterprise_root(uint8_t *buf);
/* Traps */
void trap_powerFailure(uint8_t *managerIP, uint8_t *agentIP);
void trap_powerRestored(uint8_t *managerIP, uint8_t *agentIP);
void trap_overTemperature(uint8_t *managerIP, uint8_t *agentIP);
void trap_overCurrent(uint8_t *managerIP, uint8_t *agentIP);

/* ======= Trap: fault thay đổi (AC/OP_OFF/OTP/OLP) ======= */
void snmp_trap_state_reset(void);
void snmp_process_fault_trap(uint8_t *managerIP, uint8_t *agentIP);

/* ======= Trap: theo SPEC 5.1–5.2 ======= */
void snmp_trap_policy_reset(void);
/* Gọi mỗi 100–500ms với dữ liệu hiện tại */
void snmp_trap_process_spec(uint8_t *managerIP, uint8_t *agentIP,
                            uint8_t ac_present,
                            float batt_v, float soc_pct, float runtime_min, float temp_c,
                            uint8_t overload, uint8_t hw_fault, uint8_t bms_fault);


#ifdef __cplusplus
}
#endif

#endif
