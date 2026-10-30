#include "smart_cocis.h"
#include "driver/ledc.h"
static portMUX_TYPE feedback_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t open_until;
static int64_t sound_until;
static bool last_granted;
/* Convert a servo pulse width in microseconds into the configured PWM duty. */
static void servo_pulse(int microseconds)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, microseconds * 8191 / 20000));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}
/* Record independent door and sound deadlines so later denials never cancel an opening. */
void door_feedback(bool granted)
{
    int64_t current = now_ms();
    portENTER_CRITICAL(&feedback_lock);
    if (granted) open_until = current + 3000;
    sound_until = current + (granted ? 120 : 600);
    last_granted = granted;
    portEXIT_CRITICAL(&feedback_lock);
}
/* Configure 50 Hz servo PWM and a separate 2 kHz passive buzzer timer. */
void door_initialize(void)
{
    ledc_timer_config_t servo_timer = { .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT, .timer_num = LEDC_TIMER_0, .freq_hz = 50, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&servo_timer));
    ledc_channel_config_t servo = { .gpio_num = SERVO_PIN, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0 };
    ESP_ERROR_CHECK(ledc_channel_config(&servo));
    ledc_timer_config_t sound_timer = { .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT, .timer_num = LEDC_TIMER_1, .freq_hz = 2000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&sound_timer));
    ledc_channel_config_t buzzer = { .gpio_num = BUZZER_PIN, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1, .timer_sel = LEDC_TIMER_1 };
    ESP_ERROR_CHECK(ledc_channel_config(&buzzer));
    servo_pulse(500);
}
/* Close the door after three seconds and produce short success or pulsed denial tones. */
void door_task(void *argument)
{
    (void)argument;
    bool previous_open = false;
    int previous_duty = -1;
    while (true) {
        int64_t current = now_ms();
        portENTER_CRITICAL(&feedback_lock);
        bool opened = current < open_until;
        bool sounding = current < sound_until;
        bool granted = last_granted;
        portEXIT_CRITICAL(&feedback_lock);
        if (opened != previous_open) {
            servo_pulse(opened ? 1500 : 500);
            previous_open = opened;
        }
        int duty = sounding && (granted || (current / 100) % 2 == 0) ? 512 : 0;
        if (duty != previous_duty) {
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
            previous_duty = duty;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
