#include "smart_cocis.h"
/* Poll both readers and emit one crossing per presentation, even during menus. */
void entrance_task(void *argument)
{
    (void)argument;
    char last_uid[2][UID_SIZE] = {{0}};
    int64_t last_seen[2] = {0};
    while (true) {
        for (int reader = 0; reader < 2; reader++) {
            card_event_t card = { .exit_reader = reader == 1 };
            if (rfid_read(reader, card.uid)) {
                card.scanned_at = now_ms();
                if (strcmp(last_uid[reader], card.uid) != 0) {
                    strcpy(last_uid[reader], card.uid);
                    ESP_LOGI("RFID", "%s card UID=%s", reader == 0 ? "ENTRY" : "EXIT", card.uid);
                    process_access(&card);
                    if (xQueueSend(menu_card_queue, &card, 0) != pdPASS) {
                        ESP_LOGW("RFID", "Menu scan queue full; normal access still processed");
                    }
                }
                last_seen[reader] = now_ms();
            } else if (now_ms() - last_seen[reader] > 400) {
                last_uid[reader][0] = '\0';
            }
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
