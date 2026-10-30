#include "smart_cocis.h"
/* Prepare the system and launch independent tasks for every ongoing job. */
void app_main(void)
{
    system_initialize();
    start_task(storage_task, "csv_logger", 4096, NULL, 2);
    start_task(door_task, "door_buzzer", 3072, NULL, 3);
    start_task(entrance_task, "entry_exit_rfid", 4096, NULL, 4);
    start_task(room1_task, "room_one", 3072, NULL, 3);
    start_task(room2_task, "room_two", 3072, NULL, 3);
    start_task(admin_menu_task, "lcd_keypad_menu", 6144, NULL, 2);
    start_task(serial_menu_task, "serial_input", 3072, NULL, 1);
    ESP_LOGI("CoCIS", "Ready. Press keypad 1 for management. Demo clock started at 07:00 EAT.");
}
