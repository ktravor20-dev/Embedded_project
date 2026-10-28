#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    printf("Smart CoCIS System Started!\n");

    while (1)
    {
        printf("ESP32-S3 is running...\n");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}