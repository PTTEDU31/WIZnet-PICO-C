#ifndef DEV_MODBUS_H
#define DEV_MODBUS_H

#include <stdint.h>
#include <stdbool.h>

#include "../config/hardware_config.h"
#include "../psu_data/psu_data.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================
// Modbus Function Codes
// ============================
#define MODBUS_FC_READ_HOLDING   0x03
#define MODBUS_FC_READ_INPUT     0x04
#define MODBUS_FC_WRITE_SINGLE   0x06

// ============================
// Modbus Common Registers (example subset)
// ============================
#define REG_OPERATION        0x0000
#define REG_VOUT_SET         0x0020
#define REG_READ_VIN         0x0050
#define REG_READ_VOUT        0x0060
#define RED_READ_IOUT        0X0061
#define RED_READ_VBAT        0X00D3
#define RED_READ_IBAT        0X00D4

#define REG_CURVE_CC         0x00B0
#define REG_CURVE_CV         0x00B1
#define REG_CURVE_FV         0x00B2
#define REG_CURVE_TC         0x00B3

#define RED_TEMP             0X0062

#define REG_FAULT_STATUS     0x0040
#define REG_SYSTEM_STATUS    0x00C3


// ============================
// Core API
// ============================
bool modbus_poll_fault_status(uint8_t slave_id, uint16_t *status_raw, fault_flags_t *flags);

/**
 * @brief Tính CRC16 chuẩn Modbus
 */
uint16_t modbus_crc16(const uint8_t *buf, uint16_t len);

/**
 * @brief Gửi yêu cầu đọc 1 hoặc nhiều thanh ghi
 * @param slave_id   Địa chỉ thiết bị Modbus
 * @param func_code  Function code (0x03 / 0x04)
 * @param reg_addr   Địa chỉ bắt đầu
 * @param quantity   Số lượng thanh ghi muốn đọc
 * @param rx_buf     Buffer chứa dữ liệu phản hồi
 * @param timeout_ms Thời gian timeout
 * @return true nếu OK, false nếu lỗi hoặc timeout
 */
bool modbus_read_registers(uint8_t slave_id, uint8_t func_code,
                           uint16_t reg_addr, uint16_t quantity,
                           uint8_t *rx_buf, uint32_t timeout_ms);

/**
 * @brief Đọc 1 thanh ghi 16-bit (trả về uint16_t)
 */
bool modbus_read_u16(uint8_t slave_id, uint8_t func_code,
                     uint16_t reg_addr, uint16_t *value);

/**
 * @brief Đọc thanh ghi dạng float (theo hệ số scale)
 */
bool modbus_read_float(uint8_t slave_id, uint8_t func_code,
                       uint16_t reg_addr, float *value, float scale);

/**
 * @brief Ghi 1 thanh ghi 16-bit (function 0x06)
 */
bool modbus_write_u16(uint8_t slave_id, uint16_t reg_addr, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif // DEV_MODBUS_H
