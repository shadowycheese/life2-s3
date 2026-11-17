#ifndef TASK_H
#define TASK_H
#include "freertos/FreeRTOS.h"
class Task
{
  public:
    virtual void init() = 0;
    virtual bool run(bool overlay_visible, uint64_t now, TickType_t *sleep_time) = 0;
    virtual const char *configuration_name() = 0;
    virtual size_t get_configuration_size() = 0;
    virtual size_t get_configuration(void *buffer) = 0;
    virtual void set_configuration(void *configuration) = 0;
    virtual size_t get_state(void *buffer) = 0;
};

#endif