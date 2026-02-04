// SPDX-License-Identifier: MIT
#include "sdkconfig.h"

#if !CONFIG_ZB_GP_ENABLED

#include <stdbool.h>
#include "zgp/zgp_internal.h"

/*
 * Stub required by the prebuilt Zigbee API archive even when GP is disabled.
 * We return false to indicate "not handled".
 */
bool zb_zcl_green_power_cluster_handler(zb_uint8_t param)
{
    (void)param;
    return false;
}

#endif /* !CONFIG_ZB_GP_ENABLED */

