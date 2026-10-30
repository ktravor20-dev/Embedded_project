#include "smart_cocis.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "nvs_flash.h"
system_state_t system_state;
SemaphoreHandle_t state_mutex;
QueueHandle_t menu_card_queue;
QueueHandle_t serial_queue;
QueueHandle_t log_queue;
/* Return monotonic time for timeouts that must not depend on clock changes. */
int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}
/* Create a task and stop startup if there is not enough memory. */
void start_task(TaskFunction_t function, const char *name, uint32_t stack, void *argument, UBaseType_t priority)
{
    configASSERT(xTaskCreate(function, name, stack, argument, priority, NULL) == pdPASS);
}
/* Initialize shared resources once, with all SPI chip-select lines initially high. */
void system_initialize(void)
{
    state_mutex = xSemaphoreCreateMutex();
    menu_card_queue = xQueueCreate(8, sizeof(card_event_t));
    serial_queue = xQueueCreate(128, sizeof(char));
    log_queue = xQueueCreate(32, sizeof(log_event_t));
    configASSERT(state_mutex && menu_card_queue && serial_queue && log_queue);
    strcpy(system_state.access_status, "Ready to scan");
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        ESP_LOGE("NVS", "Initialization failed; refusing to erase saved data: %s", esp_err_to_name(result));
        ESP_ERROR_CHECK(result);
    }
    schedule_load();
    setenv("TZ", "EAT-3", 1);
    tzset();
    configASSERT(clock_initialize());
    int chip_selects[] = { ENTRY_CS, EXIT_CS, SD_CS };
    for (int index = 0; index < 3; index++) {
        gpio_reset_pin(chip_selects[index]);
        gpio_set_direction(chip_selects[index], GPIO_MODE_OUTPUT);
        gpio_set_level(chip_selects[index], 1);
    }
    spi_bus_config_t bus = {
        .mosi_io_num = SPI_MOSI, .miso_io_num = SPI_MISO,
        .sclk_io_num = SPI_CLOCK, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    /* Put the SD card into SPI mode before talking to either RFID reader. */
    storage_initialize();
    storage_load_users();
    rfid_initialize();
    door_initialize();
    rooms_initialize();
    keypad_initialize();
    lcd_initialize();
    queue_log("access.csv", "SYSTEM,BOOT,RESTART_OCCUPANCY_ZERO,0");
}
