#include "smart_cocis.h"
#include "driver/gpio.h"
/* Apply the requested light state to room 1's output. */
void room1_light(bool enabled)
{
    gpio_set_level(6, enabled);
}
