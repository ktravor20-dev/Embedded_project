#include "smart_cocis.h"
#include "nvs.h"
#include <sys/time.h>
/* Check inclusive opening and exclusive closing times, including overnight hours. */
bool minute_allowed(schedule_t schedule, int minute)
{
    if (minute < 0 || minute >= 1440 || schedule.start_minute == schedule.end_minute) return false;
    if (schedule.start_minute < schedule.end_minute) {
        return minute >= schedule.start_minute && minute < schedule.end_minute;
    }
    return minute >= schedule.start_minute || minute < schedule.end_minute;
}
/* Restore one atomic schedule value from NVS, falling back to normal class hours. */
void schedule_load(void)
{
    system_state.schedule = (schedule_t){420, 1380};
    nvs_handle_t handle;
    if (nvs_open("cocis", NVS_READONLY, &handle) != ESP_OK) return;
    uint32_t packed;
    if (nvs_get_u32(handle, "hours", &packed) == ESP_OK) {
        int start = packed >> 16;
        int end = packed & 65535;
        if (start < 1440 && end < 1440 && start != end) system_state.schedule = (schedule_t){start, end};
    }
    nvs_close(handle);
}
/* Verify admin authorization inside the setter and commit before publishing new hours. */
bool schedule_change(int start, int end)
{
    if (start < 0 || start >= 1440 || end < 0 || end >= 1440 || start == end) return false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    bool success = false;
    if (admin_authorized_locked()) {
        nvs_handle_t handle;
        if (nvs_open("cocis", NVS_READWRITE, &handle) == ESP_OK) {
            esp_err_t result = nvs_set_u32(handle, "hours", ((uint32_t)start << 16) | end);
            if (result == ESP_OK) result = nvs_commit(handle);
            success = result == ESP_OK;
            nvs_close(handle);
            if (success) system_state.schedule = (schedule_t){start, end};
        }
    }
    xSemaphoreGive(state_mutex);
    if (success) queue_log("access.csv", "SYSTEM,SETTINGS,SCHEDULE_CHANGED,0");
    return success;
}
/* Set the running demo clock on the presentation date using local East Africa time. */
static bool set_demo_time(int hour, int minute)
{
    struct tm date = { .tm_year = 126, .tm_mon = 9, .tm_mday = 30,
                       .tm_hour = hour, .tm_min = minute, .tm_isdst = -1 };
    struct timeval value = { .tv_sec = mktime(&date), .tv_usec = 0 };
    return settimeofday(&value, NULL) == 0;
}
/* Start the clock at 07:00 before launching tasks so manual setup is unnecessary. */
bool clock_initialize(void)
{
    return set_demo_time(7, 0);
}
/* Allow an authenticated admin to adjust the already-running presentation clock. */
bool clock_change(int hour, int minute)
{
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    bool success = false;
    if (admin_authorized_locked()) success = set_demo_time(hour, minute);
    xSemaphoreGive(state_mutex);
    if (success) queue_log("access.csv", "SYSTEM,CLOCK,DEMO_CLOCK_SET,0");
    return success;
}
