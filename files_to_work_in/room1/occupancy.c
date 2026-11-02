#include "smart_cocis.h"
#include "driver/gpio.h"
/* Read the motion signal from room 1's PIR sensor. */
bool room1_motion(void)
{
    return gpio_get_level(4) != 0;
}
