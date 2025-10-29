#include "dev_modbus.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

uint16_t g_fault_status = 0;
fault_flags_t g_fault_flags = {0};

// ============================
// RS485 Direction Control
// ============================
static inline void rs485_set_tx_mode(bool tx) {
    gpio_put(RS485_DIR_PIN, tx);
    sleep_us(50);
}

// ============================
// CRC16 Calculation
// ============================
uint16_t modbus_crc16(const uint8_t *buf, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < len; pos++) {
        crc ^= buf[pos];
        for (int i = 0; i < 8; i++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

// ============================
// UART Send/Receive Helpers
// ============================
static void rs485_send(const uint8_t *data, size_t len) {
    rs485_set_tx_mode(true);
    uart_write_blocking(MODBUS_UART, data, len);
    uart_tx_wait_blocking(MODBUS_UART);
    sleep_us(3500); // 3.5 char time
    rs485_set_tx_mode(false);
}

static int rs485_read(uint8_t *buf, size_t maxlen, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    size_t count = 0;
    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        if (uart_is_readable(MODBUS_UART)) {
            buf[count++] = uart_getc(MODBUS_UART);
            if (count >= maxlen) break;
        }
    }
    return count;
}

// ============================
// API Implementation
// ============================
bool modbus_read_registers(uint8_t slave_id, uint8_t func_code,
                           uint16_t reg_addr, uint16_t quantity,
                           uint8_t *rx_buf, uint32_t timeout_ms) {
    uint8_t tx_buf[8];
    tx_buf[0] = slave_id;
    tx_buf[1] = func_code;
    tx_buf[2] = reg_addr >> 8;
    tx_buf[3] = reg_addr & 0xFF;
    tx_buf[4] = quantity >> 8;
    tx_buf[5] = quantity & 0xFF;

    uint16_t crc = modbus_crc16(tx_buf, 6);
    tx_buf[6] = crc & 0xFF;
    tx_buf[7] = crc >> 8;

    // Gửi gói yêu cầu
    rs485_send(tx_buf, 8);

    // Nhận phản hồi
    int n = rs485_read(rx_buf, 64, timeout_ms);
    if (n < 5) {
        printf("[MODBUS] ❌ Timeout or too short frame (n=%d)\n", n);
        return false;
    }

    // Kiểm tra CRC
    uint16_t crc_rx = (rx_buf[n - 1] << 8) | rx_buf[n - 2];
    uint16_t crc_calc = modbus_crc16(rx_buf, n - 2);
    if (crc_rx != crc_calc) {
        printf("[MODBUS] ❌ CRC mismatch (calc=0x%04X recv=0x%04X)\n", crc_calc, crc_rx);
        return false;
    }

    // Kiểm tra Slave ID
    if (rx_buf[0] != slave_id) {
        printf("[MODBUS] ❌ Wrong Slave ID (expected=0x%02X got=0x%02X)\n", slave_id, rx_buf[0]);
        return false;
    }

    // Kiểm tra Exception
    if (rx_buf[1] & 0x80) {
        uint8_t exception_code = rx_buf[2];
        printf("[MODBUS] ❌ Exception 0x%02X from slave 0x%02X: ", exception_code, slave_id);
        switch (exception_code) {
            case 0x01: printf("Illegal Function\n"); break;
            case 0x02: printf("Illegal Data Address\n"); break;
            case 0x03: printf("Illegal Data Value\n"); break;
            case 0x04: printf("Slave Device Failure\n"); break;
            default:   printf("Unknown Error\n"); break;
        }
        return false;
    }

    // Kiểm tra Function code
    if (rx_buf[1] != func_code) {
        printf("[MODBUS] ❌ Wrong Function Code (expected=0x%02X got=0x%02X)\n",
               func_code, rx_buf[1]);
        return false;
    }

    // Kiểm tra byte count hợp lý
    if (n >= 5) {
        uint8_t byte_count = rx_buf[2];
        uint8_t expected_bytes = quantity * 2;
        if (byte_count != expected_bytes) {
            printf("[MODBUS] ⚠️ Unexpected byte count (expected=%d got=%d)\n",
                   expected_bytes, byte_count);
        }
    }

    return true;
}


bool modbus_read_u16(uint8_t slave_id, uint8_t func_code,
                     uint16_t reg_addr, uint16_t *value) {
    uint8_t rx[16];
    if (!modbus_read_registers(slave_id, func_code, reg_addr, 1, rx, 1000))
        return false;
    *value = (rx[3] << 8) | rx[4];
    return true;
}

bool modbus_read_float(uint8_t slave_id, uint8_t func_code,
                       uint16_t reg_addr, float *value, float scale) {
    uint16_t raw;
    if (!modbus_read_u16(slave_id, func_code, reg_addr, &raw))
        return false;
    *value = raw * scale;
    return true;
}

bool modbus_write_u16(uint8_t slave_id, uint16_t reg_addr, uint16_t value) {
    uint8_t tx_buf[8], rx_buf[16];
    tx_buf[0] = slave_id;
    tx_buf[1] = MODBUS_FC_WRITE_SINGLE;
    tx_buf[2] = reg_addr >> 8;
    tx_buf[3] = reg_addr & 0xFF;
    tx_buf[4] = value >> 8;
    tx_buf[5] = value & 0xFF;
    uint16_t crc = modbus_crc16(tx_buf, 6);
    tx_buf[6] = crc & 0xFF;
    tx_buf[7] = crc >> 8;
    rs485_send(tx_buf, 8);
    int n = rs485_read(rx_buf, sizeof(rx_buf), 1000);
    return (n >= 8);
}



bool modbus_poll_fault_status(uint8_t slave_id, uint16_t *status_raw, fault_flags_t *flags)
{
    uint16_t reg = 0;

    // Dùng hàm modbus_read_u16 có sẵn để đọc thanh ghi 0x0040
    if (!modbus_read_u16(slave_id, MODBUS_FC_READ_HOLDING, REG_FAULT_STATUS, &reg))
    {
        printf("[MODBUS] ❌ Failed to read FAULT_STATUS\n");
        return false;
    }

    if (status_raw)
        *status_raw = reg;

    if (flags)
    {
        flags->fan_fail      = (reg >> 0) & 1;
        flags->otp           = (reg >> 1) & 1;
        flags->ovp           = (reg >> 2) & 1;
        flags->olp           = (reg >> 3) & 1;
        flags->short_circuit = (reg >> 4) & 1;
        flags->ac_fail       = (reg >> 5) & 1;
        flags->op_off        = (reg >> 6) & 1;
    }

    printf("[MODBUS] FAULT=0x%04X | FAN=%d | OTP=%d | OVP=%d | OLP=%d | SHORT=%d | AC_FAIL=%d | OP_OFF=%d\n",
           reg,
           flags->fan_fail,
           flags->otp,
           flags->ovp,
           flags->olp,
           flags->short_circuit,
           flags->ac_fail,
           flags->op_off);

    return true;
}