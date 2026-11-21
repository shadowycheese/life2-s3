#ifndef TASK_H
#define TASK_H
#include "freertos/FreeRTOS.h"
class Task
{
  public:
    virtual void init() = 0;
    virtual bool run(uint64_t now, TickType_t *sleep_time) = 0;
};

#endif