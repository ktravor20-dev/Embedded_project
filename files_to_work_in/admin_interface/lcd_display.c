#include "smart_cocis.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
static i2c_master_dev_handle_t lcd;
static bool lcd_ready;
/* Pulse the LCD enable line with one four-bit command or data nibble. */
static bool lcd_nibble(uint8_t nibble, bool data)
{
    uint8_t value = (nibble & 0xf0) | 0x08 | (data ? 1 : 0);
    uint8_t bytes[] = { value, value | 4, value };
    return i2c_master_transmit(lcd, bytes, sizeof(bytes), 50) == ESP_OK;
}
/* Send a complete LCD byte as two consecutive four-bit writes. */
static void lcd_byte(uint8_t value, bool data)
{
    if (!lcd_ready) return;
    if (!lcd_nibble(value, data) || !lcd_nibble(value << 4, data)) lcd_ready = false;
    esp_rom_delay_us(50);
}
/* Initialize the PCF8574 backpack at address 0x27 in four-bit display mode. */
void lcd_initialize(void)
{
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t config = { .i2c_port = I2C_NUM_0,
        .sda_io_num = LCD_SDA, .scl_io_num = LCD_SCL, .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&config, &bus));
    if (i2c_master_probe(bus, 0x27, 100) != ESP_OK) {
        ESP_LOGE("LCD", "No display at I2C address 0x27; serial interface remains available");
        return;
    }
    i2c_device_config_t device = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x27, .scl_speed_hz = 100000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &device, &lcd));
    lcd_ready = true;
    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_nibble(0x30, false);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_nibble(0x30, false);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_nibble(0x30, false);
    lcd_nibble(0x20, false);
    lcd_byte(0x28, false);
    lcd_byte(0x0c, false);
    lcd_byte(0x06, false);
    lcd_byte(0x01, false);
    vTaskDelay(pdMS_TO_TICKS(3));
}
/* Pad and write exactly sixteen characters on each LCD line without clearing. */
void lcd_show(const char *first, const char *second)
{
    const char *lines[] = { first, second };
    for (int row = 0; row < 2; row++) {
        lcd_byte(row == 0 ? 0x80 : 0xc0, false);
        size_t length = strlen(lines[row]);
        for (int column = 0; column < 16; column++) lcd_byte(column < length ? lines[row][column] : ' ', true);
    }
}
