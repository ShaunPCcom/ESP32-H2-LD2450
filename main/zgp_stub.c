#include <stdbool.h>
#include "zboss_api.h"

/* Required by libzboss_stack.zczr.a (zgp_cluster.c.obj) */
bool zb_zcl_green_power_cluster_handler(zb_uint8_t param)
{
    (void)param;
    return false; /* "not handled" */
}

