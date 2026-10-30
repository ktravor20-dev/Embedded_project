#include "smart_cocis.h"
/* Run the monitoring loop for room two. */
void room2_task(void *argument)
{
    (void)argument;
    monitor_room(1);
}
