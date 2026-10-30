#ifndef SMART_COCIS_H
#define SMART_COCIS_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#define MAX_USERS 100
#define UID_SIZE 21
#define NAME_SIZE 25
#define SPI_MOSI 11
#define SPI_MISO 13
#define SPI_CLOCK 12
#define ENTRY_CS 10
#define EXIT_CS 9
#define SD_CS 14
#define RFID_RESET 15
#define SERVO_PIN 16
#define BUZZER_PIN 17
#define LCD_SDA 8
#define LCD_SCL 18
#define ROOM_TIMEOUT_SECONDS 30
#define SESSION_SECONDS 60
#define REGISTRATION_PASSWORD "admin"
#define ADMIN_PASSWORD "admin"
typedef enum { STUDENT, LECTURER, ADMIN } user_role_t;
typedef struct {
    char uid[UID_SIZE];
    char name[NAME_SIZE];
    user_role_t role;
    bool active;
    bool inside;
} user_t;
typedef struct { int start_minute; int end_minute; } schedule_t;
typedef struct { bool motion; bool light; } room_state_t;
typedef struct { char uid[UID_SIZE]; bool exit_reader; int64_t scanned_at; } card_event_t;
typedef struct { char filename[20]; char line[192]; } log_event_t;
typedef struct {
    user_t users[MAX_USERS];
    int user_count;
    int occupancy;
    schedule_t schedule;
    room_state_t rooms[2];
    char access_status[17];
    char access_name[NAME_SIZE];
    char access_result[17];
    int64_t access_display_until;
    int64_t admin_until;
} system_state_t;
extern system_state_t system_state;
extern SemaphoreHandle_t state_mutex;
extern QueueHandle_t menu_card_queue;
extern QueueHandle_t serial_queue;
extern QueueHandle_t log_queue;
extern atomic_bool storage_ready;
extern atomic_uint dropped_logs;
/* Return milliseconds since the controller started. */
int64_t now_ms(void);
/* Initialize shared hardware, storage, settings and communication queues. */
void system_initialize(void);
/* Create one checked FreeRTOS task. */
void start_task(TaskFunction_t function, const char *name, uint32_t stack, void *argument, UBaseType_t priority);
/* Return a readable role name for a user role. */
const char *role_name(user_role_t role);
/* Validate a UID written as uppercase hexadecimal without separators. */
bool valid_uid(const char *uid);
/* Find a registered user while the caller holds the state mutex. */
int find_user_locked(const char *uid);
/* Register and persist a user using the separate registration password. */
bool register_user(const char *password, const char *uid, const char *name, user_role_t role);
/* Verify an active ADMIN card and password and start a short session. */
bool authenticate_admin(const card_event_t *card, const char *password);
/* Check the admin session while the caller holds the state mutex. */
bool admin_authorized_locked(void);
/* End the current admin session. */
void admin_logout(void);
/* Check a minute against a same-day or overnight schedule. */
bool minute_allowed(schedule_t schedule, int minute);
/* Start the demo clock automatically at 07:00 on each boot. */
bool clock_initialize(void);
/* Load the saved access hours or use 07:00 to 23:00. */
void schedule_load(void);
/* Save new access hours only for an authenticated admin. */
bool schedule_change(int start, int end);
/* Set the local demo clock only for an authenticated admin. */
bool clock_change(int hour, int minute);
/* Decide access for one card and update occupancy under the state lock. */
void process_access(const card_event_t *card);
/* Mount the SD card on the already initialized shared bus. */
void storage_initialize(void);
/* Read and validate registered users before starting tasks. */
void storage_load_users(void);
/* Append one registered user and report write failures. */
bool storage_append_user(const user_t *user);
/* Queue a CSV log without delaying sensor or access tasks. */
void queue_log(const char *filename, const char *details);
/* Write queued log records to the SD card. */
void storage_task(void *argument);
/* Display a bounded recent log view on the serial monitor. */
void storage_show_logs(void);
/* Initialize both readers without reinitializing their shared SPI bus. */
void rfid_initialize(void);
/* Read one 4, 7 or 10 byte card UID from a reader. */
bool rfid_read(int reader, char *uid);
/* Scan both readers continuously and deliver scans to access and menus. */
void entrance_task(void *argument);
/* Request a door opening or denial tone without blocking the reader. */
void door_feedback(bool granted);
/* Set up servo and buzzer PWM. */
void door_initialize(void);
/* Operate the door and buzzer without blocking RFID scanning. */
void door_task(void *argument);
/* Set up the LCD using the ESP-IDF I2C master driver. */
void lcd_initialize(void);
/* Write two padded lines to the LCD from the UI task. */
void lcd_show(const char *first, const char *second);
/* Configure keypad rows and columns. */
void keypad_initialize(void);
/* Return a debounced key once per press. */
char keypad_read(void);
/* Run the default dashboard and nonblocking management state machine. */
void admin_menu_task(void *argument);
/* Forward serial characters to the UI task without changing settings. */
void serial_menu_task(void *argument);
/* Configure both room sensors and light outputs. */
void rooms_initialize(void);
/* Monitor room one motion and inactivity. */
void room1_task(void *argument);
/* Monitor room two motion and inactivity. */
void room2_task(void *argument);
/* Run the shared room monitoring algorithm for one room. */
void monitor_room(int room);
/* Read the first room PIR input. */
bool room1_motion(void);
/* Read the second room PIR input. */
bool room2_motion(void);
/* Switch the first room light. */
void room1_light(bool enabled);
/* Switch the second room light. */
void room2_light(bool enabled);
#endif
