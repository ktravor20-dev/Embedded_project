#include "smart_cocis.h"
#include "nvs.h"
#include <assert.h>
#include <sys/time.h>
system_state_t system_state;
SemaphoreHandle_t state_mutex;
atomic_bool storage_ready;
atomic_uint dropped_logs;
QueueHandle_t menu_card_queue;
QueueHandle_t serial_queue;
static int64_t fake_milliseconds = 1000;
static bool disk_ok = true;
static bool feedback;
static int writes;
static int nvs_result;
static uint32_t saved_hours;
static time_t fake_seconds = 36000;
/* Supply deterministic monotonic time for session expiry tests. */
int64_t now_ms(void) { return fake_milliseconds; }
/* Replace disk writes with a controllable success or failure. */
bool storage_append_user(const user_t *user) { (void)user; writes++; return disk_ok; }
/* Accept logs without touching a real SD card. */
void queue_log(const char *file, const char *details) { (void)file; (void)details; }
/* Capture the actuator request produced by an access decision. */
void door_feedback(bool granted) { feedback = granted; }
/* Supply a deterministic wall clock for schedule checks. */
time_t test_time(time_t *output) { if (output) *output = fake_seconds; return fake_seconds; }
/* Simulate a successful clock change without changing the computer clock. */
int test_settimeofday(const struct timeval *value, const void *zone) { (void)zone; fake_seconds = value->tv_sec; return 0; }
/* Simulate opening the NVS namespace. */
int nvs_open(const char *name, int mode, nvs_handle_t *handle) { (void)name; (void)mode; *handle = 1; return nvs_result; }
/* Return the saved schedule for restoration tests. */
int nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *value) { (void)handle; (void)key; *value = saved_hours; return nvs_result; }
/* Capture the schedule submitted to persistent storage. */
int nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value) { (void)handle; (void)key; saved_hours = value; return nvs_result; }
/* Simulate committing a settings transaction. */
int nvs_commit(nvs_handle_t handle) { (void)handle; return nvs_result; }
/* Close the fake NVS handle. */
void nvs_close(nvs_handle_t handle) { (void)handle; }
#include "../files_to_work_in/authentication/authentication.c"
#define settimeofday test_settimeofday
#include "../files_to_work_in/access_schedule/access_schedule.c"
#undef settimeofday
#define time test_time
#include "../files_to_work_in/access_control/access_control.c"
#undef time
static char display_first[32];
static char display_second[32];
/* Capture LCD lines for tests of the actual menu and scan display. */
void lcd_show(const char *first, const char *second)
{
    snprintf(display_first, sizeof(display_first), "%s", first);
    snprintf(display_second, sizeof(display_second), "%s", second);
}
/* Return no key when the task-level polling interface is not under test. */
char keypad_read(void) { return 0; }
/* Replace the log viewer during host menu tests. */
void storage_show_logs(void) { }
#include "../files_to_work_in/admin_interface/admin_menu.c"
/* Exercise real core modules against deterministic storage, clock and actuator substitutes. */
int main(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    /* No saved schedule and no admin clock setup are needed for the first entry. */
    nvs_result = 1;
    schedule_load();
    assert(system_state.schedule.start_minute == 420);
    assert(system_state.schedule.end_minute == 1380);
    nvs_result = 0;
    assert(clock_initialize());
    struct tm boot_time;
    localtime_r(&fake_seconds, &boot_time);
    assert(boot_time.tm_hour == 7 && boot_time.tm_min == 0);
    assert(register_user("admin", "ABCDEF01", "Boot test", STUDENT));
    card_event_t boot_card = {.uid="ABCDEF01"};
    process_access(&boot_card);
    assert(feedback && system_state.occupancy == 1);
    boot_card.exit_reader = true;
    process_access(&boot_card);
    assert(feedback && system_state.occupancy == 0);
    for (int minute = 0; minute < 1440; minute++) {
        assert(minute_allowed(system_state.schedule, minute) == (minute >= 420 && minute < 1380));
    }
    memset(&system_state, 0, sizeof(system_state));
    writes = 0;
    puts("PASS: automatic 07:00 clock, default 07:00-23:00 hours, entry and exit without manual clock setup.");
    schedule_t daytime = {480,1080};
    schedule_t overnight = {1320,360};
    for (int minute = 0; minute < 1440; minute++) {
        assert(minute_allowed(daytime, minute) == (minute >= 480 && minute < 1080));
        assert(minute_allowed(overnight, minute) == (minute >= 1320 || minute < 360));
    }
    assert(!minute_allowed((schedule_t){0,0}, 0));
    assert(!minute_allowed(daytime, -1));
    assert(!valid_uid("0102"));
    assert(!valid_uid("aabbccdd"));
    assert(valid_uid("04112233445566"));
    assert(valid_uid("00112233445566778899"));
    assert(!register_user("wrong", "01020304", "Admin", ADMIN));
    assert(writes == 0);
    assert(register_user("admin", "01020304", "Admin", ADMIN));
    assert(!register_user("admin", "01020304", "Again", ADMIN));
    assert(!register_user("admin", "11223344", "Bad,Name", STUDENT));
    disk_ok = false;
    assert(!register_user("admin", "11223344", "Student", STUDENT));
    assert(system_state.user_count == 1);
    disk_ok = true;
    assert(register_user("admin", "11223344", "Student", STUDENT));
    assert(register_user("admin", "55667788", "Lecturer", LECTURER));
    card_event_t admin = {.uid="01020304", .scanned_at=1000};
    card_event_t student = {.uid="11223344", .scanned_at=1000};
    assert(!schedule_change(480,1080));
    assert(!clock_change(10,0));
    assert(!authenticate_admin(&student, "admin"));
    assert(!authenticate_admin(&admin, "wrong"));
    assert(authenticate_admin(&admin, "admin"));
    assert(schedule_change(480,1080));
    assert(!schedule_change(1440,1080));
    assert(!schedule_change(480,480));
    nvs_result = 1;
    assert(!schedule_change(600,1000));
    assert(system_state.schedule.start_minute == 480);
    nvs_result = 0;
    assert(schedule_change(480,1080));
    system_state.schedule = (schedule_t){1,2};
    schedule_load();
    assert(system_state.schedule.start_minute == 480);
    assert(system_state.schedule.end_minute == 1080);
    assert(!clock_change(24,0));
    assert(clock_change(10,0));
    fake_seconds = 36000;
    process_access(&student);
    assert(feedback && system_state.occupancy == 1);
    process_access(&student);
    assert(!feedback && system_state.occupancy == 1);
    student.exit_reader = true;
    fake_seconds = 19 * 3600;
    process_access(&student);
    assert(!feedback && system_state.occupancy == 1);
    fake_seconds = 10 * 3600;
    process_access(&student);
    assert(feedback && system_state.occupancy == 0);
    process_access(&student);
    assert(!feedback && system_state.occupancy == 0);
    student.exit_reader = false;
    system_state.users[1].active = false;
    process_access(&student);
    assert(!feedback);
    card_event_t unknown = {.uid="DEADBEEF"};
    process_access(&unknown);
    assert(!feedback && system_state.occupancy == 0);
    fake_milliseconds = 62000;
    assert(!schedule_change(600,1000));
    assert(!authenticate_admin(&admin, "admin"));
    admin.scanned_at = 63000;
    assert(!authenticate_admin(&admin, "admin"));
    admin.scanned_at = 62000;
    assert(authenticate_admin(&admin, "admin"));
    admin_logout();
    assert(!schedule_change(600,1000));
    /* Exercise the real keypad-only bootstrap from an empty registration file. */
    memset(&system_state, 0, sizeof(system_state));
    change_stage(HOME);
    handle_input('1', false);
    assert(stage == HOME);
    handle_input('2', true);
    assert(stage == HOME);
    handle_input('1', true);
    assert(stage == MANAGEMENT);
    handle_input('1', true);
    assert(stage == REGISTER_PASSWORD);
    const char *password_keys = "ABCD0#";
    for (int i = 0; password_keys[i]; i++) handle_input(password_keys[i], true);
    assert(stage == REGISTER_ROLE);
    handle_input('3', true);
    assert(stage == REGISTER_CARD);
    admin.scanned_at = fake_milliseconds;
    handle_card(&admin);
    assert(stage == REGISTER_NAME);
    const char *typed_name = "Travox";
    for (int i = 0; typed_name[i]; i++) handle_input(typed_name[i], false);
    handle_input('\b', false);
    handle_input('r', false);
    render_display();
    assert(strcmp(display_second, "Travor") == 0);
    handle_input('#', true);
    assert(strcmp(system_state.users[0].name, "Travor") == 0);
    assert(stage == MESSAGE && system_state.user_count == 1 && system_state.users[0].role == ADMIN);
    change_stage(MANAGEMENT);
    handle_input('2', true);
    assert(stage == ADMIN_CARD);
    handle_card(&admin);
    assert(stage == ADMIN_PASS);
    for (int i = 0; password_keys[i]; i++) handle_input(password_keys[i], true);
    assert(stage == SETTINGS);
    handle_input('1', true);
    const char *hours = "08001800#";
    for (int i = 0; hours[i]; i++) handle_input(hours[i], true);
    assert(stage == MESSAGE && system_state.schedule.start_minute == 480);
    change_stage(SETTINGS);
    handle_input('2', true);
    const char *clock_keys = "1000#";
    for (int i = 0; clock_keys[i]; i++) handle_input(clock_keys[i], true);
    assert(stage == MESSAGE);
    change_stage(MANAGEMENT);
    fake_seconds = 36000;
    process_access(&admin);
    assert(feedback && system_state.occupancy == 1 && stage == MANAGEMENT);
    handle_input('*', true);
    assert(stage == HOME && !admin_authorized_locked());
    /* Verify named scan feedback, denials, unknown cards and return to the same menu. */
    strcpy(system_state.users[0].name, "Travor");
    system_state.users[0].inside = false;
    system_state.occupancy = 0;
    fake_seconds = 36000;
    change_stage(MANAGEMENT);
    process_access(&admin);
    render_display();
    assert(strcmp(display_first, "Travor") == 0);
    assert(strcmp(display_second, "Access granted") == 0);
    assert(stage == MANAGEMENT);
    process_access(&admin);
    render_display();
    assert(strcmp(display_first, "Travor") == 0);
    assert(strcmp(display_second, "Access denied") == 0);
    process_access(&unknown);
    render_display();
    assert(strcmp(display_first, "Unknown card") == 0);
    assert(strcmp(display_second, "Access denied") == 0);
    fake_milliseconds += 3000;
    render_display();
    assert(stage == MANAGEMENT);
    assert(strcmp(display_first, "Unknown card") != 0);
    puts("PASS: LCD name and grant/deny feedback, unknown card, and menu restoration after three seconds.");
    puts("PASS: real keypad bootstrap, ADMIN menu login, schedule/clock forms, RFID access during menu, and logout.");
    puts("PASS: 2,880 schedule-minute cases; registration, ADMIN authorization, NVS failure/restore, clock validation, entry/exit, duplicates, inactive/unknown cards, expiry and logout.");
}
