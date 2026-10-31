#include "smart_cocis.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
static const int row_pins[] = {1, 2, 3, 21};
static const int column_pins[] = {39, 40, 41, 42};
static const char keys[4][4] = {{'1','2','3','A'}, {'4','5','6','B'}, {'7','8','9','C'}, {'*','0','#','D'}};
/* Set open-drain rows high and enable column pull-ups for safe matrix scanning. */
void keypad_initialize(void)
{
    for (int index = 0; index < 4; index++) {
        gpio_reset_pin(row_pins[index]);
        gpio_set_direction(row_pins[index], GPIO_MODE_OUTPUT_OD);
        gpio_set_level(row_pins[index], 1);
        gpio_reset_pin(column_pins[index]);
        gpio_set_direction(column_pins[index], GPIO_MODE_INPUT);
        gpio_set_pull_mode(column_pins[index], GPIO_PULLUP_ONLY);
    }
}
/* Reject multiple simultaneous keys and debounce both press and release for forty milliseconds. */
char keypad_read(void)
{
    static char candidate;
    static char stable;
    static int64_t changed_at;
    char pressed = 0;
    int count = 0;
    for (int row = 0; row < 4; row++) {
        gpio_set_level(row_pins[row], 0);
        esp_rom_delay_us(5);
        for (int column = 0; column < 4; column++) {
            if (!gpio_get_level(column_pins[column])) { pressed = keys[row][column]; count++; }
        }
        gpio_set_level(row_pins[row], 1);
    }
    if (count > 1) pressed = 0;
    if (candidate != pressed) { candidate = pressed; changed_at = now_ms(); }
    if (stable != candidate && now_ms() - changed_at >= 40) {
        stable = candidate;
        return stable;
    }
    return 0;
}
