#include "smart_cocis.h"
#include <ctype.h>
typedef enum {
    HOME, MANAGEMENT, REGISTER_PASSWORD, REGISTER_ROLE, REGISTER_CARD, REGISTER_NAME,
    ADMIN_CARD, ADMIN_PASS, SETTINGS, CHANGE_SCHEDULE, CHANGE_CLOCK, MESSAGE
} menu_stage_t;
static menu_stage_t stage;
static menu_stage_t after_message;
static char field[48];
static size_t field_length;
static bool field_overflow;
static user_role_t selected_role;
static card_event_t selected_card;
static int64_t stage_started;
static int64_t last_activity;
static char message_first[17];
static char message_second[17];
/* Start a menu stage and clear text left by a previous password or field. */
static void change_stage(menu_stage_t next)
{
    stage = next;
    stage_started = now_ms();
    last_activity = now_ms();
    memset(field, 0, sizeof(field));
    field_length = 0;
    field_overflow = false;
    if (next == HOME) admin_logout();
    if (next == MANAGEMENT) printf("\n1 Register | 2 Admin settings | 3 Status | 4 Access logs | 5 Users | * Exit\n");
    if (next == REGISTER_PASSWORD || next == ADMIN_PASS) printf("Password: type admin + Enter, or keypad A B C D 0 #. *=cancel\n");
    if (next == REGISTER_ROLE) printf("Role: 1 STUDENT, 2 LECTURER, 3 ADMIN\n");
    if (next == REGISTER_CARD || next == ADMIN_CARD) printf("Present card at either RFID reader (normal access also stays active).\n");
    if (next == REGISTER_NAME) printf("Name: type up to 24 characters + Enter; keypad # uses User-UID.\n");
    if (next == SETTINGS) printf("ADMIN: 1 Schedule | 2 Demo clock | *=logout. Session expires after 60 seconds.\n");
    if (next == CHANGE_SCHEDULE) printf("Enter opening and closing HHMMHHMM then # or Enter (e.g. 08001800).\n");
    if (next == CHANGE_CLOCK) printf("Enter demo clock HHMM then # or Enter. Date: 2026-10-30 EAT.\n");
}
/* Show a short result then return to a chosen stage without blocking other tasks. */
static void show_message(const char *first, const char *second, menu_stage_t next)
{
    snprintf(message_first, sizeof(message_first), "%s", first);
    snprintf(message_second, sizeof(message_second), "%s", second);
    printf("%s: %s\n", first, second);
    change_stage(MESSAGE);
    after_message = next;
}
/* Print registered users and current occupancy from a consistent snapshot. */
static void print_status(bool users)
{
    static system_state_t snapshot;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    snapshot = system_state;
    xSemaphoreGive(state_mutex);
    printf("People inside: %d | Clock: %s | SD: %s | Lost logs: %u\n", snapshot.occupancy,
           "DEMO EAT", storage_ready ? "ready" : "failed", (unsigned)atomic_load(&dropped_logs));
    printf("Hours %02d:%02d-%02d:%02d | Room1 PIR=%d Light=%d | Room2 PIR=%d Light=%d\n",
           snapshot.schedule.start_minute / 60, snapshot.schedule.start_minute % 60,
           snapshot.schedule.end_minute / 60, snapshot.schedule.end_minute % 60,
           snapshot.rooms[0].motion, snapshot.rooms[0].light, snapshot.rooms[1].motion, snapshot.rooms[1].light);
    if (users) {
        for (int index = 0; index < snapshot.user_count; index++) {
            user_t *user = &snapshot.users[index];
            printf("%s %s %s active=%d %s\n", user->uid, user->name, role_name(user->role), user->active, user->inside ? "INSIDE" : "OUTSIDE");
        }
    }
}
/* Convert exactly four decimal digits into valid minutes after midnight. */
static int parse_hhmm(const char *text)
{
    for (int index = 0; index < 4; index++) if (!isdigit((unsigned char)text[index])) return -1;
    int hour = (text[0] - '0') * 10 + text[1] - '0';
    int minute = (text[2] - '0') * 10 + text[3] - '0';
    if (hour > 23 || minute > 59) return -1;
    return hour * 60 + minute;
}
/* Validate a completed form and call the protected registration or settings API. */
static void submit_field(void)
{
    if (field_overflow) { show_message("Input too long", "Try again", MANAGEMENT); return; }
    if (stage == REGISTER_PASSWORD) {
        if (strcmp(field, REGISTRATION_PASSWORD) == 0) change_stage(REGISTER_ROLE);
        else show_message("Wrong password", "Not registered", MANAGEMENT);
    } else if (stage == ADMIN_PASS) {
        if (authenticate_admin(&selected_card, field)) change_stage(SETTINGS);
        else show_message("Admin denied", "Card + password", MANAGEMENT);
    } else if (stage == REGISTER_NAME) {
        char name[NAME_SIZE];
        if (field_length == 0) snprintf(name, sizeof(name), "User-%s", selected_card.uid + strlen(selected_card.uid) - 8);
        else snprintf(name, sizeof(name), "%.*s", NAME_SIZE - 1, field);
        bool success = field_length < NAME_SIZE && register_user(REGISTRATION_PASSWORD, selected_card.uid, name, selected_role);
        show_message(success ? "User registered" : "Register failed", success ? role_name(selected_role) : "Duplicate/SD/name", MANAGEMENT);
    } else if (stage == CHANGE_SCHEDULE) {
        int start = field_length == 8 ? parse_hhmm(field) : -1;
        int end = field_length == 8 ? parse_hhmm(field + 4) : -1;
        bool success = schedule_change(start, end);
        show_message(success ? "Hours saved" : "Hours rejected", success ? "Saved in NVS" : "Check input/auth", SETTINGS);
    } else if (stage == CHANGE_CLOCK) {
        int minute = field_length == 4 ? parse_hhmm(field) : -1;
        bool success = minute >= 0 && clock_change(minute / 60, minute % 60);
        show_message(success ? "Demo clock set" : "Clock rejected", "2026-10-30 EAT", SETTINGS);
    }
}
/* Interpret one key or serial character according to the current menu stage. */
static void handle_input(char key, bool from_keypad)
{
    if (stage == HOME) {
        if (from_keypad && key == '1') change_stage(MANAGEMENT);
        return;
    }
    if (key == '\r') key = '\n';
    if (key == '*') { change_stage(HOME); return; }
    last_activity = now_ms();
    if (stage == MANAGEMENT) {
        if (key == '1') change_stage(REGISTER_PASSWORD);
        if (key == '2') { admin_logout(); change_stage(ADMIN_CARD); }
        if (key == '3') { print_status(false); show_message("Status printed", "See serial", MANAGEMENT); }
        if (key == '4') { storage_show_logs(); show_message("Access logs", "See serial", MANAGEMENT); }
        if (key == '5') { print_status(true); show_message("Users printed", "See serial", MANAGEMENT); }
        return;
    }
    if (stage == SETTINGS) {
        if (key == '1') change_stage(CHANGE_SCHEDULE);
        if (key == '2') change_stage(CHANGE_CLOCK);
        return;
    }
    if (stage == REGISTER_ROLE) {
        if (key >= '1' && key <= '3') { selected_role = key - '1'; change_stage(REGISTER_CARD); }
        return;
    }
    if (stage != REGISTER_PASSWORD && stage != ADMIN_PASS && stage != REGISTER_NAME &&
        stage != CHANGE_SCHEDULE && stage != CHANGE_CLOCK) return;
    if (key == '#' || key == '\n') {
        if (stage == REGISTER_NAME) { putchar('\n'); fflush(stdout); }
        submit_field();
        return;
    }
    if (key == '\b' || key == 127) {
        if (field_length) {
            field[--field_length] = '\0';
            if (stage == REGISTER_NAME && !from_keypad) { printf("\b \b"); fflush(stdout); }
        }
        return;
    }
    if (from_keypad && (stage == REGISTER_PASSWORD || stage == ADMIN_PASS)) {
        if (key == 'A') key = 'a';
        else if (key == 'B') key = 'd';
        else if (key == 'C') key = 'm';
        else if (key == 'D') key = 'i';
        else if (key == '0') key = 'n';
    }
    if ((unsigned char)key >= 32 && (unsigned char)key < 127) {
        if (field_length < sizeof(field) - 1) {
            field[field_length++] = key;
            field[field_length] = '\0';
            if (stage == REGISTER_NAME && !from_keypad) { putchar(key); fflush(stdout); }
        } else field_overflow = true;
    }
}
/* Accept a fresh reader event only when the menu is explicitly waiting for a card. */
static void handle_card(const card_event_t *card)
{
    if (card->scanned_at < stage_started) return;
    if (stage == REGISTER_CARD) {
        selected_card = *card;
        change_stage(REGISTER_NAME);
    } else if (stage == ADMIN_CARD) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        int index = find_user_locked(card->uid);
        bool valid = index >= 0 && system_state.users[index].active && system_state.users[index].role == ADMIN;
        xSemaphoreGive(state_mutex);
        if (valid) { selected_card = *card; change_stage(ADMIN_PASS); }
        else show_message("ADMIN required", "Card rejected", MANAGEMENT);
    }
}
/* Render a rotating live dashboard or the current management prompt. */
static void render_display(void)
{
    char first[32] = "";
    char second[32] = "";
    /* Briefly show the latest scan without changing the underlying menu or stopping tasks. */
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    bool show_scan = now_ms() < system_state.access_display_until;
    if (show_scan) {
        int offset = 0;
        int length = strlen(system_state.access_name);
        if (length > 16) {
            offset = (now_ms() - (system_state.access_display_until - 3000)) / 300;
            if (offset > length - 16) offset = length - 16;
            if (offset < 0) offset = 0;
        }
        snprintf(first, sizeof(first), "%.16s", system_state.access_name + offset);
        snprintf(second, sizeof(second), "%s", system_state.access_result);
    }
    xSemaphoreGive(state_mutex);
    if (show_scan) {
        lcd_show(first, second);
        return;
    }
    if (stage == HOME) {
        int page = (now_ms() / 2500) % 5;
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        if (page == 0) {
            strcpy(first, "People inside");
            snprintf(second, sizeof(second), "%d 1:Menu SD:%s", system_state.occupancy, storage_ready ? "OK" : "!");
        } else if (page == 1 || page == 2) {
            room_state_t room = system_state.rooms[page - 1];
            snprintf(first, sizeof(first), "Room %d PIR:%s", page, room.motion ? "YES" : "NO");
            snprintf(second, sizeof(second), "Light:%s 1:Menu", room.light ? "ON" : "OFF");
        } else if (page == 3) {
            snprintf(first, sizeof(first), "%s", system_state.access_status);
            snprintf(second, sizeof(second), "RFID active");
        } else {
            strcpy(first, "Allowed time");
            snprintf(second, sizeof(second), "%02d:%02d - %02d:%02d",
                system_state.schedule.start_minute / 60, system_state.schedule.start_minute % 60,
                system_state.schedule.end_minute / 60, system_state.schedule.end_minute % 60);
        }
        xSemaphoreGive(state_mutex);
    } else if (stage == MANAGEMENT) {
        int page = (now_ms() / 2000) % 3;
        if (page == 0) { strcpy(first, "1:Register user"); strcpy(second, "2:Admin settings"); }
        if (page == 1) { strcpy(first, "3:Status 4:Logs"); strcpy(second, "5:Users *:Exit"); }
        if (page == 2) { strcpy(first, "RFID still active"); strcpy(second, "*:Dashboard"); }
    } else if (stage == REGISTER_PASSWORD || stage == ADMIN_PASS) {
        strcpy(first, stage == ADMIN_PASS ? "Admin password" : "Register passwd");
        snprintf(second, sizeof(second), "%.*s #=OK", (int)(field_length > 10 ? 10 : field_length), "**********");
    } else if (stage == REGISTER_ROLE) { strcpy(first, "1:STUD 2:LECT"); strcpy(second, "3:ADMIN *:Exit"); }
    else if (stage == REGISTER_CARD) { strcpy(first, "Scan NEW card"); strcpy(second, "Either reader"); }
    else if (stage == REGISTER_NAME) {
        strcpy(first, "Name: Enter=save");
        if (field_length) {
            size_t offset = field_length > 16 ? field_length - 16 : 0;
            snprintf(second, sizeof(second), "%.16s", field + offset);
        } else strcpy(second, "#:Use UID name");
    }
    else if (stage == ADMIN_CARD) { strcpy(first, "Scan ADMIN card"); strcpy(second, "Then password"); }
    else if (stage == SETTINGS) { strcpy(first, "1:Hours 2:Clock"); strcpy(second, "*:Logout"); }
    else if (stage == CHANGE_SCHEDULE) { strcpy(first, "Hours HHMMHHMM"); snprintf(second, sizeof(second), "%.12s #OK", field); }
    else if (stage == CHANGE_CLOCK) { strcpy(first, "Demo time HHMM"); snprintf(second, sizeof(second), "%.12s #OK", field); }
    else if (stage == MESSAGE) { strcpy(first, message_first); strcpy(second, message_second); }
    lcd_show(first, second);
}
/* Run menu input, card capture and LCD updates without sleeping inside a form. */
void admin_menu_task(void *argument)
{
    (void)argument;
    change_stage(HOME);
    int64_t last_display = -1000;
    while (true) {
        char key = keypad_read();
        if (key) handle_input(key, true);
        char serial;
        for (int index = 0; index < 8 && xQueueReceive(serial_queue, &serial, 0) == pdPASS; index++) handle_input(serial, false);
        card_event_t card;
        while (xQueueReceive(menu_card_queue, &card, 0) == pdPASS) handle_card(&card);
        if (stage == MESSAGE && now_ms() - stage_started >= 1800) change_stage(after_message);
        if (stage != HOME && now_ms() - last_activity > 60000) change_stage(HOME);
        if (stage == SETTINGS || stage == CHANGE_SCHEDULE || stage == CHANGE_CLOCK) {
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            bool authorized = admin_authorized_locked();
            xSemaphoreGive(state_mutex);
            if (!authorized) show_message("Session expired", "Scan admin again", MANAGEMENT);
        }
        if (now_ms() - last_display >= 250) { render_display(); last_display = now_ms(); }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
