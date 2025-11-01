#include "led_status.h"
#include "../../config/hardware_config.h"
#include "hardware/pwm.h"// LED nội bộ
static device_state_t dev_state = DEV_OFF;
static uint32_t last_toggle = 0;
static uint32_t last_state_change = 0;
static uint8_t led_value = 0;
static uint8_t blink_count = 0;

// =========================
// INIT
// =========================
void led_init(void) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
    dev_state = DEV_OFF;
    last_toggle = last_state_change = to_ms_since_boot(get_absolute_time());
}

// =========================
// CHUYỂN TRẠNG THÁI
// =========================
void led_set_state(device_state_t new_state) {
    dev_state = new_state;
    blink_count = 0;
    led_value = 0;
    last_toggle = last_state_change = to_ms_since_boot(get_absolute_time());
    gpio_put(LED_PIN, 0);
}

// =========================
// CẬP NHẬT LED (non-blocking)
// =========================
void led_update(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t delta = now - last_toggle;

    switch (dev_state) {
    case DEV_OFF:
        gpio_put(LED_PIN, 0);
        break;

    case DEV_BOOTING:
        // Nháy nhanh liên tục trong 3 giây
        if (now - last_state_change < 3000) {
            if (delta >= 150) {
                led_value = !led_value;
                gpio_put(LED_PIN, led_value);
                last_toggle = now;
            }
        } else {
            led_set_state(DEV_RUNNING);
        }
        break;

    case DEV_RUNNING:
        // Nháy chậm 1Hz
        if (delta >= 500) {
            led_value = !led_value;
            gpio_put(LED_PIN, led_value);
            last_toggle = now;
        }
        break;

    case DEV_ERROR:
        // Nháy nhanh 3 lần rồi nghỉ 1s
        if (delta >= 150) {
            led_value = !led_value;
            gpio_put(LED_PIN, led_value);
            last_toggle = now;
            if (led_value) blink_count++;
            if (blink_count >= 6) { // 3 lần (on/off)
                blink_count = 0;
                sleep_ms(300); // nghỉ ngắn (non-critical)
            }
        }
        break;

    case DEV_UPDATING:
        // Sáng dần / tắt dần (hiệu ứng "thở")
        {
            static int brightness = 0;
            static int dir = 5;
            static absolute_time_t last_breathe;
            if (absolute_time_diff_us(last_breathe, get_absolute_time()) > 10 * 1000) {
                brightness += dir;
                if (brightness >= 255 || brightness <= 0) dir = -dir;
                pwm_set_gpio_level(LED_PIN, brightness * brightness / 255);
                last_breathe = get_absolute_time();
            }
        }
        break;

    case DEV_IDLE:
        // Sáng 1s rồi tắt 3s
        if (delta >= (led_value ? 1000 : 3000)) {
            led_value = !led_value;
            gpio_put(LED_PIN, led_value);
            last_toggle = now;
        }
        break;

    case DEV_CUSTOM_PATTERN:
        // Bạn có thể tự thêm logic riêng tại đây
        break;
    }
}
