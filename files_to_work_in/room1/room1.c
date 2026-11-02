#include "smart_cocis.h"
#include "driver/gpio.h"
/* Configure both PIR inputs and start both room lights in the off state. */
void rooms_initialize(void)
{
    int inputs[] = {4, 5};
    int outputs[] = {6, 7};
    for (int index = 0; index < 2; index++) {
        gpio_reset_pin(inputs[index]);
        gpio_set_direction(inputs[index], GPIO_MODE_INPUT);
        gpio_reset_pin(outputs[index]);
        gpio_set_direction(outputs[index], GPIO_MODE_OUTPUT);
        gpio_set_level(outputs[index], 0);
    }
}
/* Track motion and switch a light off only after the inactivity timeout expires. */
void monitor_room(int room)
{
    int64_t last_motion = 0;
    bool seen_motion = false;
    bool previous_motion = false;
    bool previous_light = false;
    while (true) {
        bool motion = room == 0 ? room1_motion() : room2_motion();
        if (motion) { last_motion = now_ms(); seen_motion = true; }
        bool light = seen_motion && now_ms() - last_motion < ROOM_TIMEOUT_SECONDS * 1000;
        if (room == 0) room1_light(light);
        else room2_light(light);
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        system_state.rooms[room] = (room_state_t){ motion, light };
        xSemaphoreGive(state_mutex);
        if (motion != previous_motion || light != previous_light) {
            char details[64];
            snprintf(details, sizeof(details), "%d,%d,%d", room + 1, motion, light);
            queue_log("rooms.csv", details);
            previous_motion = motion;
            previous_light = light;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
/* Run the monitoring loop for room one. */
void room1_task(void *argument)
{
    (void)argument;
    monitor_room(0);
}
