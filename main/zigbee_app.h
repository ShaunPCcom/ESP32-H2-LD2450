#pragma once
#include <stdbool.h>
#include <stdint.h>

void zigbee_app_start(void);

/* For later: drive ZCL attrs from sensor core */
void zigbee_app_set_occupied(bool occupied);
void zigbee_app_set_target_count(uint8_t count);
