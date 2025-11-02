#include "led_status.h"
#include "../../config/hardware_config.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "led_status.pio.h"
#include <stdio.h>

static PIO pio;
static uint sm;
static uint offset;
static device_state_t dev_state = DEV_OFF;

// =====================================================
// Nạp chương trình PIO LED
// =====================================================
static void led_status_hw_init(uint pin)
{
    // Ưu tiên dùng PIO1 (tránh xung đột với SPI0 / W5500)
    PIO pio_sel = pio1;
    if (pio_sm_is_claimed(pio_sel, 0) && pio_sm_is_claimed(pio_sel, 1) &&
        pio_sm_is_claimed(pio_sel, 2) && pio_sm_is_claimed(pio_sel, 3))
    {
        pio_sel = pio0; // fallback nếu pio1 đầy
    }

    // Nạp chương trình và claim state machine
    uint offset_local = pio_add_program(pio_sel, &led_status_program);
    uint sm_local = pio_claim_unused_sm(pio_sel, true);
    hard_assert(sm_local < 4);

    // Khởi tạo hardware cho LED
    led_status_program_init(pio_sel, sm_local, offset_local, pin);
    pio_sm_set_enabled(pio_sel, sm_local, true);

    // Lưu thông tin toàn cục
    pio = pio_sel;
    sm = sm_local;
    offset = offset_local;

    printf("[PIO] LED program loaded on PIO%d SM%d at offset %u (pin %u)\n",
           PIO_NUM(pio_sel), sm_local, offset_local, pin);
}

// =====================================================
// Gửi chu kỳ blink (Hz) cho PIO — FIXED VERSION
// =====================================================
void led_set_blink_hz(float freq_hz)
{
    if (freq_hz <= 0) freq_hz = 1.0f;
    uint32_t period = clock_get_hz(clk_sys) / (2 * freq_hz);

    // Đảm bảo SM được khởi động lại
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_set_enabled(pio, sm, true);

    // Gửi dữ liệu on/off delay
    pio_sm_put_blocking(pio, sm, period);
    pio_sm_put_blocking(pio, sm, period);
}

// =====================================================
// Khởi tạo LED
// =====================================================
void led_init(void)
{
    led_status_hw_init(LED_PIN);
    dev_state = DEV_OFF;
    gpio_put(LED_PIN, 0);
}

// =====================================================
// Đặt trạng thái LED
// =====================================================
void led_set_state(device_state_t new_state)
{
    dev_state = new_state;

    switch (new_state)
    {
    case DEV_OFF:
        pio_sm_set_enabled(pio, sm, false);
        gpio_put(LED_PIN, 0);
        break;

    case DEV_BOOTING:
        pio_sm_set_enabled(pio, sm, true);
        led_set_blink_hz(5.0f); // nháy nhanh
        break;

    case DEV_RUNNING:
        pio_sm_set_enabled(pio, sm, true);
        led_set_blink_hz(1.0f); // nháy đều 1Hz
        break;

    case DEV_ERROR:
        pio_sm_set_enabled(pio, sm, true);
        led_set_blink_hz(8.0f); // nháy nhanh hơn
        break;

    case DEV_IDLE:
        pio_sm_set_enabled(pio, sm, true);
        led_set_blink_hz(0.5f); // sáng 1s tắt 1s
        break;

    case DEV_UPDATING:
    {
        // Hiệu ứng "thở" dùng PWM phần cứng
        pio_sm_set_enabled(pio, sm, false);
        gpio_set_function(LED_PIN, GPIO_FUNC_PWM);

        uint slice = pwm_gpio_to_slice_num(LED_PIN);
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv(&cfg, 64.f);
        pwm_init(slice, &cfg, true);

        // vòng breathing (có thể chuyển sang non-blocking task nếu muốn)
        for (int i = 0; i < 255; i += 2) {
            pwm_set_gpio_level(LED_PIN, i * i);
            sleep_ms(5);
        }
        for (int i = 255; i >= 0; i -= 2) {
            pwm_set_gpio_level(LED_PIN, i * i);
            sleep_ms(5);
        }

        // Sau khi breathing xong quay lại chế độ PIO
        gpio_set_function(LED_PIN, GPIO_FUNC_PIO1);
        pio_sm_restart(pio, sm);
        pio_sm_set_enabled(pio, sm, true);
        led_set_blink_hz(1.0f);
        break;
    }

    default:
        break;
    }
}

// =====================================================
// Không cần update() nữa — PIO hoạt động độc lập
// =====================================================
void led_update(void) {}
