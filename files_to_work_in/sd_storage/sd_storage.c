#include "smart_cocis.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <errno.h>
atomic_bool storage_ready;
atomic_uint dropped_logs;
static SemaphoreHandle_t storage_mutex;
/* Create a CSV file header only when the file does not already exist. */
static bool ensure_file(const char *path, const char *header)
{
    FILE *file = fopen(path, "r");
    if (file) { fclose(file); return true; }
    if (errno != ENOENT) return false;
    file = fopen(path, "w");
    if (!file) return false;
    bool success = fprintf(file, "%s\n", header) >= 0;
    if (fclose(file) != 0) success = false;
    return success;
}
/* Mount FAT without formatting or erasing an existing SD card. */
void storage_initialize(void)
{
    storage_mutex = xSemaphoreCreateMutex();
    configASSERT(storage_mutex);
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 4000;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SPI2_HOST;
    device.gpio_cs = SD_CS;
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 4096
    };
    sdmmc_card_t *card;
    esp_err_t result = esp_vfs_fat_sdspi_mount("/sdcard", &host, &device, &mount, &card);
    if (result != ESP_OK) {
        ESP_LOGE("SD", "Mount failed: %s. Registration unavailable; no users loaded.", esp_err_to_name(result));
        return;
    }
    storage_ready = ensure_file("/sdcard/users.csv", "CardID,Name,Role,Active") &&
        ensure_file("/sdcard/access.csv", "Timestamp,CardID,Direction,Result,Occupancy") &&
        ensure_file("/sdcard/rooms.csv", "Timestamp,Room,Motion,Light");
    ESP_LOGI("SD", "Storage %s", storage_ready ? "ready" : "file initialization failed");
}
/* Validate every users.csv row and skip malformed or repeated records at startup. */
void storage_load_users(void)
{
    if (!storage_ready) return;
    FILE *file = fopen("/sdcard/users.csv", "r");
    if (!file) { storage_ready = false; return; }
    char line[160];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "CardID,", 7) == 0) continue;
        char uid[UID_SIZE], name[NAME_SIZE], role[16], active[8], extra;
        int count = sscanf(line, "%20[^,],%24[^,],%15[^,],%7[^\r\n]%c", uid, name, role, active, &extra);
        if (count < 4 || !valid_uid(uid) || (strcmp(active, "0") && strcmp(active, "1"))) {
            ESP_LOGW("SD", "Skipped malformed user row");
            continue;
        }
        user_role_t parsed_role;
        if (strcmp(role, "STUDENT") == 0) parsed_role = STUDENT;
        else if (strcmp(role, "LECTURER") == 0) parsed_role = LECTURER;
        else if (strcmp(role, "ADMIN") == 0) parsed_role = ADMIN;
        else continue;
        if (find_user_locked(uid) >= 0 || system_state.user_count >= MAX_USERS) continue;
        user_t *user = &system_state.users[system_state.user_count++];
        strcpy(user->uid, uid);
        strcpy(user->name, name);
        user->role = parsed_role;
        user->active = strcmp(active, "1") == 0;
    }
    if (ferror(file)) storage_ready = false;
    fclose(file);
    ESP_LOGI("SD", "Loaded %d users; occupancy starts at zero", system_state.user_count);
}
/* Append and close a complete user record while serializing all SD file access. */
bool storage_append_user(const user_t *user)
{
    if (!storage_ready) return false;
    xSemaphoreTake(storage_mutex, portMAX_DELAY);
    FILE *file = fopen("/sdcard/users.csv", "a");
    bool success = false;
    if (file) {
        success = fprintf(file, "%s,%s,%s,1\n", user->uid, user->name, role_name(user->role)) >= 0;
        if (fclose(file) != 0) success = false;
    }
    if (!success) storage_ready = false;
    xSemaphoreGive(storage_mutex);
    return success;
}
/* Capture an event timestamp and enqueue the log, counting any queue overflow. */
void queue_log(const char *filename, const char *details)
{
    log_event_t event;
    snprintf(event.filename, sizeof(event.filename), "%s", filename);
    time_t current_time = time(NULL);
    struct tm local_time;
    localtime_r(&current_time, &local_time);
    char timestamp[32];
    if (local_time.tm_year < 120) snprintf(timestamp, sizeof(timestamp), "UNSET+%lldms", (long long)now_ms());
    else strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);
    snprintf(event.line, sizeof(event.line), "%s,%s\n", timestamp, details);
    if (xQueueSend(log_queue, &event, 0) != pdPASS) atomic_fetch_add(&dropped_logs, 1);
}
/* Consume CSV events in one task so access and room tasks never wait for log writes. */
void storage_task(void *argument)
{
    (void)argument;
    log_event_t event;
    while (true) {
        if (xQueueReceive(log_queue, &event, portMAX_DELAY) != pdPASS) continue;
        bool success = false;
        xSemaphoreTake(storage_mutex, portMAX_DELAY);
        if (storage_ready) {
            char path[48];
            snprintf(path, sizeof(path), "/sdcard/%s", event.filename);
            FILE *file = fopen(path, "a");
            if (file) {
                success = fputs(event.line, file) >= 0;
                if (fclose(file) != 0) success = false;
            }
            if (!success) storage_ready = false;
        }
        xSemaphoreGive(storage_mutex);
        if (!success) {
            atomic_fetch_add(&dropped_logs, 1);
            ESP_LOGE("SD", "Unsaved event: %s", event.line);
        }
    }
}
/* Print the first fifty access records without holding the shared system-state lock. */
void storage_show_logs(void)
{
    xSemaphoreTake(storage_mutex, portMAX_DELAY);
    FILE *file = storage_ready ? fopen("/sdcard/access.csv", "r") : NULL;
    if (file) {
        char line[192];
        for (int index = 0; index < 51 && fgets(line, sizeof(line), file); index++) printf("%s", line);
        fclose(file);
        printf("View limited to first 50 events; full history is on SD.\n");
    } else printf("SD logs unavailable.\n");
    xSemaphoreGive(storage_mutex);
}
