idf_component_register(
    SRCS "main.c"
        "../files_to_work_in/access_schedule/access_schedule.c"
        "../files_to_work_in/system_manager/system_manager.c"
        "../files_to_work_in/sd_storage/sd_storage.c"
        "../files_to_work_in/room2/lighting.c"
        "../files_to_work_in/room2/room2.c"
        "../files_to_work_in/room2/occupancy.c"
        "../files_to_work_in/access_control/access_control.c"
        "../files_to_work_in/room1/room1.c"
        "../files_to_work_in/room1/lighting.c"
        "../files_to_work_in/room1/occupancy.c"
        "../files_to_work_in/admin_interface/lcd_display.c"
        "../files_to_work_in/admin_interface/keypad.c"
        "../files_to_work_in/admin_interface/serial_menu.c"
        "../files_to_work_in/admin_interface/admin_menu.c"
        "../files_to_work_in/authentication/authentication.c"
        "../files_to_work_in/entrance/door_control.c"
        "../files_to_work_in/entrance/entrance.c"
        "../files_to_work_in/entrance/rfid.c"
    INCLUDE_DIRS "." "../include"
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_sdspi esp_driver_i2c
             esp_driver_ledc esp_timer fatfs sdmmc nvs_flash
)
