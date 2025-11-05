/**
 * RP2040 Modbus RTU Master + W5500 Ethernet (Web + SNTP + PSU Monitor)
 * - UART0: RS-485 (Modbus RTU)
 * - UART1: debug/log output
 * - HTTP server on port 80
 */

#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/sync.h"

#include "config/common.h"
#include "wizchip_conf.h"
#include "wizchip_spi.h"

#include "devices/eth/dev_eth.h"
#include "devices/modbus/dev_modbus.h"
#include "devices/eth/dev_web.h"
#include "devices/eth/snmp_custom.h"
#include "devices/psu_data/psu_data.h"
#include "devices/led_status/led_status.h"
#include "devices/config/app_config.h"
#include "cJSON.h"

static mutex_t g_flash_mutex;

// ============================
// Core1 Stack
// ============================
__attribute__((aligned(16))) static uint32_t core1_stack[16384 / sizeof(uint32_t)];

// ===========================================================
// RS485 Direction Control
// ===========================================================
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

// ===========================================================
// Heartbeat giám sát core1
// ===========================================================
volatile uint32_t g_core1_heartbeat_ms = 0;
#define CORE1_HB_INTERVAL_MS 500
#define CORE1_HB_TIMEOUT_MS 5000

// ===========================================================
// Bộ nhớ
// ===========================================================
extern char __end__[];
static size_t get_free_ram_bytes(void)
{
    char top;
    struct mallinfo mi = mallinfo();
    size_t free_heap = (size_t)mi.fordblks;
    size_t stack_room = (size_t)(&top - __end__);
    return free_heap ? free_heap + stack_room : stack_room;
}

static void dump_mem_usage(const char *tag)
{
    struct mallinfo mi = mallinfo();
    printf("[MEM] %s: free_heap=%d, used_heap=%d, ram_free_est=%u bytes\n",
           tag, mi.fordblks, mi.uordblks, (unsigned)get_free_ram_bytes());
}

// ===========================================================
// Safe reboot / reset
// ===========================================================
void safe_reboot(void)
{
    gpio_init(RESET_PIN);
    gpio_set_dir(RESET_PIN, GPIO_OUT);
    gpio_put(RESET_PIN, 1);
    gpio_put(RESET_PIN, 0);
}

// ===========================================================
// Core1 Entry (Modbus Polling)
// ===========================================================

/* Gọi nhiều lần để vượt debounce/rate-limit một cách nhanh gọn */
static inline void step_spec(uint8_t *mgr, uint8_t *agt,
                             uint8_t ac, float vbat, float soc, float rt_min, float tc,
                             uint8_t overload, uint8_t hwf, uint8_t bmsf,
                             int repeats, int delay_ms)
{
    for (int i = 0; i < repeats; ++i)
    {
        snmp_trap_process_spec(mgr, agt, ac, vbat, soc, rt_min, tc, overload, hwf, bmsf);
        sleep_ms(delay_ms);
    }
}

/* Hàm TEST SNMP – giả lập đủ kịch bản trap */
void snmp_trap_test_demo(uint8_t managerIP[4])
{
    printf("\n[SNMP TEST] Sending demo traps with Severity...\n");
    printf("[SNMP] Tick = %lu (%lu s uptime)\n", getSNMPTimeTick(), getSNMPTimeTick() / 100);

    snmp_send_trap_custom(managerIP, NULL, 103); // Warning
    sleep_ms(500);
    printf("[SNMP] Tick = %lu  (uptime %.1f s)\n",
           getSNMPTimeTick(),
           getSNMPTimeTick() / 100.0f);
    snmp_send_trap_custom(managerIP, NULL, 113); // Critical
    sleep_ms(500);
    printf("[SNMP] Tick = %lu  (uptime %.1f s)\n",
           getSNMPTimeTick(),
           getSNMPTimeTick() / 100.0f);
    snmp_send_trap_custom(managerIP, NULL, 101); // Normal

    printf("[SNMP TEST] Done.\n");
}

void core1_main(void)
{
    // ===========================================================
    // 1️⃣ Khởi tạo an toàn vùng flash (tránh xung đột core0)
    // ===========================================================
    flash_safe_execute_core_init();
    mutex_enter_blocking(&g_flash_mutex);
    mutex_exit(&g_flash_mutex);
    // ===========================================================
    // 2️⃣ Khởi tạo UART0 RS485
    // ===========================================================
    uart_init(MODBUS_UART, MODBUS_BAUDRATE);
    uart_set_format(MODBUS_UART, 8, 1, UART_PARITY_NONE);
    gpio_set_function(MODBUS_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(MODBUS_RX_PIN, GPIO_FUNC_UART);

    gpio_init(RS485_DIR_PIN);
    gpio_set_dir(RS485_DIR_PIN, GPIO_OUT);
    rs485_set_tx_mode(false);

    // ===========================================================
    // 3️⃣ LED khởi tạo và báo trạng thái
    // ===========================================================
    led_init();
    led_set_state(DEV_BOOTING);
    printf("[CORE1] Starting loop...\n");

    // ===========================================================
    // 4️⃣ Lấy config hệ thống 1 lần (chỉ đọc)
    // ===========================================================
    const app_config_t *cfg = app_cfg_get();

    // ===========================================================
    // 5️⃣ Biến thời gian
    // ===========================================================
    uint32_t last_hb = 0;
    uint32_t last_led = 0;
    uint32_t last_modbus = 0;
    uint32_t last_modbus_batt = 0;
    uint8_t mgr[4] = {cfg->snmp_manager_ip[0], cfg->snmp_manager_ip[1], cfg->snmp_manager_ip[2], cfg->snmp_manager_ip[3]};
    uint8_t agt[4] = {cfg->ip[0], cfg->ip[1], cfg->ip[2], cfg->ip[3]};
    // snmp_trap_test_demo(mgr);
    // snmp_trap_quick_test(mgr, agt);

    // ===========================================================
    // 6️⃣ Vòng lặp chính Core1
    // ===========================================================
    while (true)
    {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // -------------------------------------------------------
        // (A) Heartbeat: gửi nhịp cho Core0 để watchdog giám sát
        // -------------------------------------------------------
        if (now - last_hb >= CORE1_HB_INTERVAL_MS)
        {
            g_core1_heartbeat_ms = now;
            last_hb = now;
        }

        // -------------------------------------------------------
        // (B) Modbus polling định kỳ
        // -------------------------------------------------------
        if (now - last_modbus >= 1000) // 1000ms/poll
        {
            last_modbus = now;
            // printf("[MODBUS] Polling PSU slave %d...\n", cfg->psu_slave_addr);

            uint8_t slave_id = cfg->psu_slave_addr;
            psu_data_t psu_local = {0};

            modbus_read_float(slave_id, MODBUS_FC_READ_INPUT, REG_READ_VIN, &psu_local.vin, 0.1f);
            modbus_read_float(slave_id, MODBUS_FC_READ_INPUT, REG_READ_VOUT, &psu_local.vout, 0.01f);
            modbus_read_float(slave_id, MODBUS_FC_READ_INPUT, REG_READ_IOUT, &psu_local.iout, 0.01f);
            modbus_read_float(slave_id, MODBUS_FC_READ_INPUT, RED_TEMP, &psu_local.temp, 0.1f);

            modbus_read_float(slave_id, MODBUS_FC_READ_INPUT, REG_READ_VBAT, &psu_local.batt_voltage, 0.01f);
            modbus_read_float(slave_id, MODBUS_FC_READ_INPUT, REG_READ_IOUT, &psu_local.batt_current, 0.01f);
            modbus_read_float(slave_id, MODBUS_FC_READ_INPUT, REG_BAT_TEMPERATURE, &psu_local.batt_temp, 0.1f);
            // // // Fault & SNMP trap
            uint16_t fault_raw = 0;
            modbus_poll_fault_status(slave_id, &fault_raw, &psu_local.fault);
            psu_data_update(&psu_local);
            snmp_process_fault_trap(managerIP, agentIP);
        }
        if (now - last_modbus_batt >= 3000) // 300ms/poll
        {
            psu_data_t psu_local = psu_data_read();
            last_modbus_batt = now;
            uint32_t runtime = psu_calculate_runtime_minutes(cfg->psu.battery_capacity_ah,lifepo4_voltage_to_soc(psu_local.batt_voltage),psu_local.batt_current);
            printf("Runtime:  %lu min \n",(unsigned long)runtime);
            psu_cycles_print_current();
        }

        // -------------------------------------------------------
        // (D) Debug pattern: đổi trạng thái LED 5s một lần
        // -------------------------------------------------------
        if (now - last_led > 5000)
        {
            static uint8_t idx = 0;
            idx = (idx + 1) % 5;
            led_set_state((device_state_t)idx);
            last_led = now;
        }

        // -------------------------------------------------------
        // (E) Nhường CPU cho hệ thống (non-blocking)
        // -------------------------------------------------------
        tight_loop_contents();
    }
}
void start_core1(void)
{
    multicore_launch_core1_with_stack((void (*)(void))core1_main,
                                      core1_stack,
                                      sizeof(core1_stack));
}

// ===========================================================
// Main
// ===========================================================

int main(void)
{
    set_sys_clock_khz(200000, true);
    stdio_init_all();
    mutex_init(&g_flash_mutex);
    mutex_enter_blocking(&g_flash_mutex);

    
    start_core1();
    
    flash_safe_execute_core_init();
    printf("\r\n=== RP2040 Modbus PSU Monitor + W5500 (Web + SNTP + DHCP) ===\r\n");
    
    if (!app_cfg_init())
    {
        
        printf("[CFG] Invalid or empty config, using defaults.\n");
    }
    else
    printf("[CFG] Configuration loaded successfully.\n");
    psu_data_init();
    
    mutex_exit(&g_flash_mutex);
    const app_config_t *cfg = app_cfg_get();
    printf("[CFG] Device: %s | Slave: %d | DHCP: %d\n",
           cfg->device_name, cfg->psu_slave_addr, cfg->dhcp_enable);

    // Ethernet init
    eth_init();
    dev_web_init();

    // 1ms timer tick cho W5500
    struct repeating_timer rt;
    add_repeating_timer_ms(-1, repeating_timer_cb, NULL, &rt);

    dump_mem_usage("after init");

    uint32_t last_hb_check = to_ms_since_boot(get_absolute_time());
    uint32_t last_mem_log = 0;

    while (1)
    {
        eth_task();
        dev_web_task();

        watchdog_update();

        uint32_t now = to_ms_since_boot(get_absolute_time());

        // --- Giám sát Core1 ---
        if (now - last_hb_check >= 500)
        {
            last_hb_check = now;
            uint32_t hb = g_core1_heartbeat_ms;
            if (hb != 0 && (now - hb) > CORE1_HB_TIMEOUT_MS)
            {
                // printf("[WDT] Core1 heartbeat timeout (> %d ms). Restarting core1...\n",
                //        CORE1_HB_TIMEOUT_MS);
                // multicore_reset_core1();
                // sleep_ms(10);
                // multicore_launch_core1(core1_entry);
            }
        }

        // // --- Log bộ nhớ ---
        // if (now - last_mem_log >= 10000)
        // {
        //     last_mem_log = now;
        //     dump_mem_usage("periodic");
        // }
    }
}
