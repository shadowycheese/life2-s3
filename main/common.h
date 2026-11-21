#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define VOLUME_ROOT "/vol"

typedef enum : uint8_t
{
    MODE_SIM,
    MODE_GIF,
    MODE_MAND

} display_mode_t;

#ifdef __cplusplus
extern "C"
{
#endif
    void set_display_mode(display_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif