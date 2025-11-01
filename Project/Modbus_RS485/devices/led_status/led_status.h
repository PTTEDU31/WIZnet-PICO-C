#ifndef LED_STATUS_H
#define LED_STATUS_H

#include "pico/stdlib.h"

// ============================================================
// Enum định nghĩa các trạng thái thiết bị
// ============================================================
typedef enum {
    DEV_OFF = 0,        // Tắt hoàn toàn
    DEV_BOOTING,        // Đang khởi động
    DEV_RUNNING,        // Hoạt động bình thường
    DEV_ERROR,          // Báo lỗi
    DEV_UPDATING,       // Đang cập nhật firmware
    DEV_IDLE,           // Chờ hoặc không tải
    DEV_CUSTOM_PATTERN  // Pattern riêng (tùy chỉnh)
} device_state_t;

// ============================================================
// API công khai cho module LED
// ============================================================

/**
 * @brief  Khởi tạo GPIO và cấu trúc LED
 * 
 * Gọi một lần ở đầu chương trình (trong main)
 */
void led_init(void);

/**
 * @brief  Cập nhật trạng thái LED (non-blocking)
 * 
 * Gọi liên tục trong vòng lặp main() hoặc core1.
 */
void led_update(void);

/**
 * @brief  Đặt trạng thái mới cho LED
 * 
 * @param new_state  Giá trị kiểu device_state_t
 */
void led_set_state(device_state_t new_state);

/**
 * @brief  Lấy trạng thái hiện tại của LED
 * 
 * @return device_state_t 
 */
device_state_t led_get_state(void);

#endif // LED_STATUS_H
