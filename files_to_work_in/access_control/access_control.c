#include "smart_cocis.h"
/* Apply identical schedule checks in both directions and reject duplicate crossings. */
void process_access(const card_event_t *card)
{
    bool granted = false;
    const char *reason = "Unknown card";
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int index = find_user_locked(card->uid);
    time_t current_time = time(NULL);
    struct tm local_time;
    localtime_r(&current_time, &local_time);
    if (index >= 0) {
        user_t *user = &system_state.users[index];
        if (!user->active) reason = "Inactive card";
        else if (!minute_allowed(system_state.schedule, local_time.tm_hour * 60 + local_time.tm_min)) reason = "Outside hours";
        else if (card->exit_reader && !user->inside) reason = "Already outside";
        else if (!card->exit_reader && user->inside) reason = "Already inside";
        else {
            granted = true;
            user->inside = !card->exit_reader;
            if (card->exit_reader) system_state.occupancy--;
            else system_state.occupancy++;
            reason = card->exit_reader ? "Exit granted" : "Entry granted";
        }
    }
    snprintf(system_state.access_status, sizeof(system_state.access_status), "%s", reason);
    const char *name = index >= 0 ? system_state.users[index].name : "Unknown card";
    snprintf(system_state.access_name, sizeof(system_state.access_name), "%s", name);
    snprintf(system_state.access_result, sizeof(system_state.access_result), "%s",
             granted ? "Access granted" : "Access denied");
    system_state.access_display_until = now_ms() + 3000;
    int people = system_state.occupancy;
    xSemaphoreGive(state_mutex);
    char details[128];
    snprintf(details, sizeof(details), "%s,%s,%s,%d", card->uid,
             card->exit_reader ? "EXIT" : "ENTRY", reason, people);
    queue_log("access.csv", details);
    door_feedback(granted);
    ESP_LOGI("ACCESS", "%s", details);
}
