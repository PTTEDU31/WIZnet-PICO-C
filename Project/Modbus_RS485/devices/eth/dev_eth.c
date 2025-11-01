/**
 * @file dev_eth.c
 * @brief Ethernet control for RP2040 + W5500 — DHCP, SNTP, SMTP, Trap, Sensors
 */

#include "port_common.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "wizchip_conf.h"
#include "wizchip_spi.h"

#include "dhcp.h"
#include "sntp.h"
#include "hardware/timer.h"

#include "dev_eth.h"
#include "config_eth.h"
#include "snmp_custom.h"
#include "app_config.h"

/* =============================
 * Configuration
 * ============================= */
#define SOCK_DHCP 0
#define SOCK_SNTP 1
#define SNTP_PERIOD 600000 // 60s periodic sync
#define RECV_TIMEOUT 10000
static volatile uint32_t g_msec_cnt = 0;
static uint8_t g_eth_buf[2048];
static const app_config_t *cfg = NULL;
uint8_t managerIP[4];
uint8_t *agentIP;
/* Network configuration */
static wiz_NetInfo g_net_info;
/* =============================
 * Timer tick (1 ms)
 * ============================= */
void eth_1ms_tick(void)
{
    g_msec_cnt++;
    if (g_msec_cnt % 1000 == 0)
        DHCP_time_handler();
}

uint32_t millis(void)
{
    return g_msec_cnt;
}

/* =============================
 * DHCP callback
 * ============================= */
static void dhcp_assign(void)
{
    getIPfromDHCP(g_net_info.ip);
    getGWfromDHCP(g_net_info.gw);
    getSNfromDHCP(g_net_info.sn);
    getDNSfromDHCP(g_net_info.dns);

    network_initialize(g_net_info);
    printf("[DHCP] IP: %d.%d.%d.%d\n",
           g_net_info.ip[0], g_net_info.ip[1],
           g_net_info.ip[2], g_net_info.ip[3]);
    print_network_information(g_net_info);

    // eth_sntp_init();     // start SNTP after DHCP success
    // eth_sntp_get_time(); // sync once immediately
}

static void dhcp_conflict(void)
{
    printf("[DHCP] Conflict detected!\n");
}

/* =============================
 * Initialization
 * ============================= */
void eth_init(void)
{
    /* Initialize */
    cfg = app_cfg_get();

    uint8_t link_status;
    wiz_PhyConf phyconf;
    uint16_t count = 0;

    g_net_info.mac[0] = cfg->mac[0];
    g_net_info.mac[1] = cfg->mac[1];
    g_net_info.mac[2] = cfg->mac[2];
    g_net_info.mac[3] = cfg->mac[3];
    g_net_info.mac[4] = cfg->mac[4];
    g_net_info.mac[5] = cfg->mac[5];

    memcpy(g_net_info.ip, cfg->ip, 4);
    memcpy(g_net_info.sn, cfg->sn, 4);
    memcpy(g_net_info.gw, cfg->gw, 4);
    memcpy(g_net_info.dns, cfg->dns, 4);

    g_net_info.dhcp = cfg->dhcp_enable ? NETINFO_DHCP : NETINFO_STATIC;

    wizchip_spi_initialize();
    wizchip_cris_initialize();
    wizchip_reset();
    wizchip_initialize();
    wizchip_check();

    if (cfg->dhcp_enable)
    {
        printf("[ETH] DHCP mode\n");
        DHCP_init(SOCK_DHCP, g_eth_buf);
        reg_dhcp_cbfunc(dhcp_assign, dhcp_assign, dhcp_conflict);
    }
    else
    {
        printf("[ETH] Static mode\n");
        network_initialize(g_net_info);
        print_network_information(g_net_info);
    }

    /* ====== SNMP Initialization ====== */

    memcpy(managerIP, cfg->snmp_manager_ip, 4); // 🔄 dùng IP từ config
    agentIP = g_net_info.ip;           // IP thực tế của board

    printf("[SNMP] Manager IP: %d.%d.%d.%d | Agent IP: %d.%d.%d.%d\n",
           managerIP[0], managerIP[1], managerIP[2], managerIP[3],
           agentIP[0], agentIP[1], agentIP[2], agentIP[3]);

    snmpd_init(managerIP, agentIP, 2, 3); // socket SNMP agent & trap
    initial_Trap(managerIP, agentIP);     // gửi trap warmStart
}
void eth_reinit_from_config(void)
{
    const app_config_t *cfg = app_cfg_get();
    wiz_NetInfo net_info = {
        .mac = {cfg->mac[0], cfg->mac[1], cfg->mac[2], cfg->mac[3], cfg->mac[4], cfg->mac[5]},
        .ip = {cfg->ip[0], cfg->ip[1], cfg->ip[2], cfg->ip[3]},
        .sn = {cfg->sn[0], cfg->sn[1], cfg->sn[2], cfg->sn[3]},
        .gw = {cfg->gw[0], cfg->gw[1], cfg->gw[2], cfg->gw[3]},
        .dns = {cfg->dns[0], cfg->dns[1], cfg->dns[2], cfg->dns[3]},
        .dhcp = cfg->dhcp_enable ? NETINFO_DHCP : NETINFO_STATIC,
    };

    printf("[ETH] Reinitializing network...\n");
    network_initialize(net_info);
    print_network_information(net_info);

    if (cfg->dhcp_enable)
    {
        DHCP_init(SOCK_DHCP, g_eth_buf);
        reg_dhcp_cbfunc(dhcp_assign, dhcp_assign, dhcp_conflict);
        printf("[ETH] DHCP mode enabled.\n");
    }
    else
    {
        printf("[ETH] Static IP mode applied.\n");
    }

    if (cfg->sntp_enable)
    {
        eth_sntp_init();
        printf("[SNTP] Client restarted.\n");
    }
}

/* =============================
 * Periodic task
 * ============================= */
void eth_task(void)
{
    if (cfg->dhcp_enable)
        DHCP_run();

    eth_sntp_task();
    snmpd_run();
}

/* =============================
 * SNTP functions
 * ============================= */
static uint8_t sntp_buf[128];

void eth_sntp_init(void)
{
    uint8_t server_ip[4] = {133, 243, 238, 243}; // pool.ntp.org
    printf("[SNTP] Init server %d.%d.%d.%d\n",
           server_ip[0], server_ip[1], server_ip[2], server_ip[3]);
    SNTP_init(SOCK_SNTP, server_ip, 40, sntp_buf); // timezone +9 (Korea)
}

void eth_sntp_get_time(void)
{
    datetime time;
    uint8_t retval = 0;
    uint32_t start_ms = millis();

    printf("[SNTP] Requesting time...\n");
    do
    {
        retval = SNTP_run(&time);
        if (retval == 1)
            break;
    } while ((millis() - start_ms) < RECV_TIMEOUT);

    if (retval != 1)
    {
        printf("[SNTP] Failed (%d)\n", retval);
        return;
    }

    printf("[SNTP] %04d-%02d-%02d %02d:%02d:%02d\n",
           time.yy, time.mo, time.dd, time.hh, time.mm, time.ss);
}

void eth_sntp_task(void)
{
    static uint32_t last_sync = 0;
    if (millis() - last_sync < SNTP_PERIOD)
        return;
    last_sync = millis();

    datetime time;
    if (SNTP_run(&time) == 1)
    {
        printf("[SNTP] %04d-%02d-%02d %02d:%02d:%02d\n",
               time.yy, time.mo, time.dd, time.hh, time.mm, time.ss);
    }
}
