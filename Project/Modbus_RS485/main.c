/**
 * RP2040 Modbus RTU Master + W5500 Ethernet (Web + SNTP + PSU Monitor)
 * - UART0: RS-485 (Modbus RTU)
 * - UART1: debug/log output
 * - HTTP server on port 80
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/multicore.h"
#include "pico/flash.h"

#include "config/common.h"
#include "wizchip_conf.h"
#include "wizchip_spi.h"

#include "devices/eth/dev_eth.h"
#include "devices/modbus/dev_modbus.h"
#include "devices/eth/dev_web.h"
#include "devices/eth/snmp_custom.h"
#include "app_config.h"


// ==============================
// RS485 Direction Control
// ==============================
static inline void rs485_set_tx_mode(bool tx)
{
    gpio_put(RS485_DIR_PIN, tx ? 1 : 0);
    sleep_us(50); // tránh xung mép
}

/* Timer 1ms gọi eth_1ms_tick() */
static bool repeating_timer_cb(struct repeating_timer *t)
{
    eth_1ms_tick();
    return true;
}

// ==============================
// PSU Global Variables (để web đọc)
// ==============================
float g_vin = 0, g_vout = 0, g_vset = 0, g_cc = 0, g_temp = 0;

fault_flags_t fault;

// API cho /api/status
float read_vin(void) { return g_vin; }
float read_vout(void) { return g_vout; }
float read_iout(void) { return g_cc; }
float read_temp(void) { return g_temp; }
fault_flags_t read_status(void) { return fault; }

void core1_entry(void)
{
    flash_safe_execute_core_init();
    /* === Ethernet init === */
    eth_init();     // auto DHCP, SNTP, SMTP background
    dev_web_init(); // HTTP server init

    /* === Timer 1ms cho eth_tick === */
    struct repeating_timer rt;
    add_repeating_timer_ms(-1, repeating_timer_cb, NULL, &rt);

    printf("[CORE1] Starting Ethernet loop...\n");

    while (1)
    {
        // Thêm dev_eth_poll() hoặc SNTP, MQTT,… ở đây nếu cần
        eth_task();     // DHCP, SNTP, SMTP background
        dev_web_task(); // HTTP server

        sleep_ms(1);
    }
}

// ==============================
// MAIN
// ==============================

int main(void)
{
    stdio_init_all();
    flash_safe_execute_core_init();

    printf("\r\n=== RP2040 Modbus PSU Monitor + W5500 (Web + SNTP + DHCP) ===\r\n");

    // === Load Configuration from Flash ===
    if (!app_cfg_init())
    {
        printf("[CFG] Invalid or empty config, using defaults.\n");
    }
    else
    {
        printf("[CFG] Configuration loaded successfully.\n");
    }

    const app_config_t *cfg = app_cfg_get();
    printf("[CFG] Device: %s | UUID: %s | Timezone: %d | Slave: %d\n",
           cfg->device_name, cfg->uuid, cfg->timezone, cfg->psu_slave_addr);
    printf("[CFG] IP: %d.%d.%d.%d | SNMP Manager: %d.%d.%d.%d | DHCP: %d\n",
           cfg->ip[0], cfg->ip[1], cfg->ip[2], cfg->ip[3],
           cfg->snmp_manager_ip[0], cfg->snmp_manager_ip[1],
           cfg->snmp_manager_ip[2], cfg->snmp_manager_ip[3],
           cfg->dhcp_enable);

    multicore_launch_core1(core1_entry);

    /* === UART0 (Modbus RS485) === */
    uart_init(MODBUS_UART, MODBUS_BAUDRATE);
    uart_set_format(MODBUS_UART, 8, 1, UART_PARITY_NONE);
    gpio_set_function(MODBUS_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(MODBUS_RX_PIN, GPIO_FUNC_UART);

    gpio_init(RS485_DIR_PIN);
    gpio_set_dir(RS485_DIR_PIN, GPIO_OUT);
    rs485_set_tx_mode(false);

    printf("[OK] UART1 Modbus ready @ %d bps\r\n", MODBUS_BAUDRATE);

    uint8_t slave_id = cfg->psu_slave_addr;
    /* === Main Loop === */
    while (1)
    {
        // ======= MODBUS PSU POLLING =======
        modbus_read_float(slave_id, MODBUS_FC_READ_INPUT, REG_READ_VIN, &g_vin, 0.1f);
        modbus_read_float(slave_id, MODBUS_FC_READ_INPUT, REG_READ_VOUT, &g_vout, 0.01f);
        modbus_read_float(slave_id, MODBUS_FC_READ_HOLDING, REG_VOUT_SET, &g_vset, 0.01f);
        modbus_read_float(slave_id, MODBUS_FC_READ_HOLDING, REG_CURVE_CC, &g_cc, 0.001f);
        modbus_read_float(slave_id, MODBUS_FC_READ_INPUT, RED_TEMP, &g_temp, 0.001f);


        // 2️⃣ Đọc fault status và lưu vào biến toàn cục
        modbus_poll_fault_status(slave_id, &g_fault_status, &g_fault_flags);

        // 3️⃣ Xử lý trap nếu fault thay đổi
        snmp_process_fault_trap(managerIP, agentIP);

        sleep_ms(2000);
    }
}
