#ifndef SIM_MODEL_H
#define SIM_MODEL_H

#include <stdint.h>

#define SIM_CONFIG_MAGIC 0xFEEDD1ED

typedef struct
{
    float kill_rate;
    float feed_rate;
    int32_t spawn_chance;
    int32_t hue;
    int32_t magic;
} sim_configuration_t;

#endif
