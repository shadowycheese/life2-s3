#ifndef MAND_TASK_H
#define MAND_TASK_H

#include "mandlebrot/mandlebrot.h"
#include "matrix/display.h"
#include "task.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>

class MandlebrotTask : public Task
{
  public:
    MandlebrotTask(Display *display)
    {
        _display = display;
        _w = _display->width();
        _h = _display->height();
    }

    virtual void init()
    {
        _mandlebrot.init(_w, _h);
    }

    virtual bool run(bool overlay_visible, uint64_t now, TickType_t *sleep_time)
    {
        _mandlebrot.step();

        for (int y = 0; y < _h; y++)
        {
            for (int x = 0; x < _w; x++)
            {
                _display->draw_pixel(x, y, (uint16_t)_mandlebrot.iterations(x, y));
            }
        }

        if (overlay_visible)
        {
            draw_overlay();
        }

        uint64_t t = esp_timer_get_time();

        int took = (int)(t - now);

        if (took > 200000)
        {
            ESP_LOGW(APP_TAG, "Mand loop took too long [%dus]\n", took);
        }

        *sleep_time = _delay_period;

        return false;
    }

    virtual const char *configuration_name()
    {
        return "mand_config";
    }

    virtual size_t get_configuration_size()
    {
        return 0;
    }

    virtual size_t get_configuration(void *buffer)
    {
        return 0;
    }

    virtual void set_configuration(void *configuration)
    {
    }

    virtual size_t get_state(void *buffer)
    {
        return 0;
    }

  private:
    static constexpr const char *APP_TAG = "SIMT";

    const TickType_t _delay_period = pdMS_TO_TICKS(50);

    int _w, _h;

    float _x;
    float _y;
    float _size;

    Mandlebrot _mandlebrot;
    Display *_display;

    void draw_overlay()
    {
    }
};

#endif