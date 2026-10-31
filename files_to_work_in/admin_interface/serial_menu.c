#include "smart_cocis.h"
/* Pass serial input to the menu task; only that task interprets commands. */
void serial_menu_task(void *argument)
{
    (void)argument;
    while (true) {
        int value = getchar();
        if (value != EOF) {
            char character = (char)value;
            if (xQueueSend(serial_queue, &character, pdMS_TO_TICKS(20)) != pdPASS) {
                ESP_LOGW("MENU", "Serial input queue full; re-enter the field");
            }
        } else {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}
