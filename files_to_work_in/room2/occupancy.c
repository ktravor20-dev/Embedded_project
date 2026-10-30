#include "smart_cocis.h"
#include "driver/gpio.h"
/* Read the motion signal from room 2's PIR sensor. */
bool room2_motion(void)
{
    return gpio_get_level(5) != 0;
}
