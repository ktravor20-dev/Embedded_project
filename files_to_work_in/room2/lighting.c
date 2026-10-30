#include "smart_cocis.h"
#include "driver/gpio.h"
/* Apply the requested light state to room 2's output. */
void room2_light(bool enabled)
{
    gpio_set_level(7, enabled);
}
