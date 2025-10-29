#ifndef CONFIG_ETH_H
#define CONFIG_ETH_H

// #include "hardware/spi.h"

// ==============================
// Hardware Configuration (W5500)
// ==============================
// #define ETH_SPI_PORT        spi0
// #define ETH_PIN_MISO        4
// #define ETH_PIN_MOSI        7
// #define ETH_PIN_SCK         6
// #define ETH_PIN_CS          17
// #define ETH_PIN_RST         20
// #define ETH_PIN_INT         21

// #define ETH_SPI_BAUDRATE    (10 * 1000 * 1000)  // 10 MHz
/**
    ----------------------------------------------------------------------------------------------------
    Macros
    ----------------------------------------------------------------------------------------------------
*/

/* Buffer */
#define ETHERNET_BUF_MAX_SIZE (1024 * 2)

/* Socket */
#define HTTP_SOCKET_MAX_NUM 4

/* ----------------------------
 *  Local configuration
 * ---------------------------- */
#define SOCKET_SNTP 0
#define TIMEZONE    40  // Korea (example)


/**
    ----------------------------------------------------------------------------------------------------
    Variables
    ----------------------------------------------------------------------------------------------------
*/

// ==============================
// Network Configuration
// ==============================

#define _HTTPSERVER_DEBUG_  1

#define ETH_USE_DHCP        0   // 1: DHCP, 0: Static

#define ETH_MAC_ADDR        {0x00, 0x08, 0xDC, 0x11, 0x22, 0x33}
#define ETH_IP_ADDR         {192, 168, 137, 123}
#define ETH_SN_MASK         {255, 255, 255, 0}
#define ETH_GW_ADDR         {192, 168, 137, 1}
#define ETH_DNS_ADDR        {8, 8, 8, 8}

#endif
