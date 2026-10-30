#include "smart_cocis.h"
#include <ctype.h>
/* Convert the role value into the text stored in users.csv. */
const char *role_name(user_role_t role)
{
    if (role == STUDENT) return "STUDENT";
    if (role == LECTURER) return "LECTURER";
    if (role == ADMIN) return "ADMIN";
    return "INVALID";
}
/* Accept only complete 4, 7 or 10 byte uppercase hexadecimal identifiers. */
bool valid_uid(const char *uid)
{
    size_t length = strlen(uid);
    if (length != 8 && length != 14 && length != 20) return false;
    for (size_t index = 0; index < length; index++) {
        if (!isdigit((unsigned char)uid[index]) && (uid[index] < 'A' || uid[index] > 'F')) return false;
    }
    return true;
}
/* Search the user array while its caller holds the shared state mutex. */
int find_user_locked(const char *uid)
{
    for (int index = 0; index < system_state.user_count; index++) {
        if (strcmp(system_state.users[index].uid, uid) == 0) return index;
    }
    return -1;
}
/* Persist a new user before making the card usable, including the first admin. */
bool register_user(const char *password, const char *uid, const char *name, user_role_t role)
{
    if (strcmp(password, REGISTRATION_PASSWORD) != 0 || !valid_uid(uid)) return false;
    if (role < STUDENT || role > ADMIN || strlen(name) == 0 || strlen(name) >= NAME_SIZE) return false;
    for (size_t index = 0; index < strlen(name); index++) {
        if ((unsigned char)name[index] < 32 || name[index] == ',') return false;
    }
    user_t user = { .role = role, .active = true, .inside = false };
    strcpy(user.uid, uid);
    strcpy(user.name, name);
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    bool success = false;
    if (system_state.user_count < MAX_USERS && find_user_locked(uid) < 0) {
        success = storage_append_user(&user);
        if (success) system_state.users[system_state.user_count++] = user;
    }
    xSemaphoreGive(state_mutex);
    if (success) queue_log("access.csv", "SYSTEM,REGISTER,USER_ADDED,0");
    return success;
}
/* Require a recent physical ADMIN scan and the admin password for settings. */
bool authenticate_admin(const card_event_t *card, const char *password)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int index = find_user_locked(card->uid);
    bool allowed = index >= 0 && system_state.users[index].active &&
        system_state.users[index].role == ADMIN &&
        now_ms() - card->scanned_at <= 30000 && card->scanned_at <= now_ms() &&
        strcmp(password, ADMIN_PASSWORD) == 0;
    system_state.admin_until = 0;
    if (allowed) system_state.admin_until = now_ms() + SESSION_SECONDS * 1000;
    xSemaphoreGive(state_mutex);
    return allowed;
}
/* Check that the authenticated settings session has not expired. */
bool admin_authorized_locked(void)
{
    return system_state.admin_until > now_ms();
}
/* Clear authorization when the user leaves management. */
void admin_logout(void)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    system_state.admin_until = 0;
    xSemaphoreGive(state_mutex);
}
