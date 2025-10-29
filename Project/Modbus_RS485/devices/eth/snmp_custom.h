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

/* ===========================
 * Global monitored variables
 * =========================== */
extern float g_vin;   // input voltage
extern float g_vout;  // output voltage
extern float g_vset;  // set voltage
extern float g_cc;    // current limit
extern fault_flags_t g_fault_flags;   // trạng thái lỗi mới nhất
extern uint16_t g_fault_status;

/* SNMP Data Table */
extern dataEntryType snmpData[];
extern const int32_t maxData;

/* Initialization */
void initTable(void);
/* Warm Start Trap */
void initial_Trap(uint8_t *managerIP, uint8_t *agentIP);
/* Warm Start Trap */
void initial_Trap(uint8_t *managerIP, uint8_t *agentIP);
/* Traps */
void trap_powerFailure(uint8_t *managerIP, uint8_t *agentIP);
void trap_powerRestored(uint8_t *managerIP, uint8_t *agentIP);
void trap_overTemperature(uint8_t *managerIP, uint8_t *agentIP);
void trap_overCurrent(uint8_t *managerIP, uint8_t *agentIP);

/* Poll fault and send traps if changed */
void snmp_process_fault_trap(uint8_t *managerIP, uint8_t *agentIP);

#ifdef __cplusplus
}
#endif

#endif
