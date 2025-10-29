#pragma once
#include <stdint.h>
#include "wizchip_conf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Core ===== */
void eth_init(void);
void eth_task(void);
void eth_1ms_tick(void);
uint32_t millis(void);


// /* ===== DHCP ===== */
// void dhcp_assign(void);
// void dhcp_conflict(void);

/* ===== SNTP ===== */
void eth_sntp_init(void);
void eth_sntp_task(void);
void eth_sntp_get_time(void);

extern uint8_t managerIP[4];
extern uint8_t *agentIP;

#ifdef __cplusplus
}
#endif
