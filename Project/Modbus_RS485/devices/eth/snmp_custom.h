#ifndef _SNMP_CUSTOM_H_
#define _SNMP_CUSTOM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "../modbus/dev_modbus.h"   

#include "snmp.h"              // snmp_sendTrap(), dataEntryType, SNMPTRAP_WARMSTART...
/* ======= DỮ LIỆU NGOÀI (từ app/main) ======= */
/* Nếu bạn dùng psu_data.h thì thay extern này bằng psu_data_read(). */
// extern float g_vin;                // V
// extern float g_vout;               // V
// extern float g_vset;               // V
// extern float g_cc;                 // A


/* ======= ENTERPRISE ======= */
#define ENTERPRISE_ID   99999
#define COMMUNITY       "public"
#define COMMUNITY_SIZE  ((int)sizeof(COMMUNITY) - 1)

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
#define TRAP_RATELIMIT_MS       30000UL

#define RUNTIME_HYS_MIN         2.0f
#define SOC_HYS_PCT             2.0f
#define TEMP_HIGH_C             50.0f
#define TEMP_CRIT_C             60.0f
#define VBAT_EMERGENCY_V        48.0f

/* ======= MIB (bảng tối thiểu, để snmp.c không lỗi) ======= */
extern dataEntryType snmpData[];
extern const int32_t maxData;

/* ======= OID init + WarmStart + gửi trap ======= */
void snmp_custom_init_oids(void);
void initial_Trap(uint8_t *managerIP, uint8_t *agentIP);
void snmp_send_trap_custom(uint8_t *managerIP, uint8_t *agentIP, uint8_t trap_code);

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
