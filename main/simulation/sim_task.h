#ifndef SIM_TASK_H
#define SIM_TASK_H

#include "matrix/display.h"
#include "matrix/hub75_driver.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "simulation/simulation.h"
#include "task.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>

#define COMMAND_STATE 0x40
#define COMMAND_FEED_RATE 0x41
#define COMMAND_KILL_RATE 0x42
#define COMMAND_SPAWN_CHANCE 0x43
#define COMMAND_PRESET 0x44
#define COMMAND_RESET 0x45
#define COMMAND_ADD 0x46
#define COMMAND_HUE 0x47

class SimTask : public Task
{
  public:
    SimTask(Display *display) : _configuration("sim_config", sizeof(sim_configuration_t))
    {
        _display = display;
        _w = _display->width();
        _h = _display->height();
    }

    virtual void init()
    {
        _simulation.init(_w, _h);
    }

    virtual bool run(uint64_t now, TickType_t *sleep_time)
    {
        _simulation.simulate();

        for (int y = 0; y < _h; y++)
        {
            for (int x = 0; x < _w; x++)
            {
                uint16_t c = _simulation.get_colour(x, y);

                _display->draw_pixel(x, y, 0xFFF & c);
            }
        }

        if (_overlay_visible)
        {
            draw_overlay();
        }

        uint64_t t = esp_timer_get_time();

        int took = (int)(t - now);

        if (took > 50000)
        {
            ESP_LOGW(APP_TAG, "Simulator loop took too long [%dus]\n", took);
        }

        *sleep_time = _delay_period;

        return true;
    }

    virtual const char *configuration_name()
    {
        return "sim_config";
    }

    virtual size_t get_configuration_size()
    {
        return sizeof(sim_configuration_t);
    }

    virtual size_t get_configuration(void *buffer)
    {
        _simulation.get_configuration((sim_configuration_t *)buffer);

        return sizeof(sim_configuration_t);
    }

    virtual void set_configuration(void *configuration)
    {
        _simulation.set_configuration((sim_configuration_t *)configuration);
    }

    virtual size_t get_state(void *buffer)
    {
        sim_state_t *state = (sim_state_t *)buffer;

        state->feed_rate = _simulation.feed_rate();
        state->kill_rate = _simulation.kill_rate();
        state->preset = _simulation.preset();
        state->spawn_chance = _simulation.spawn_chance();

        return sizeof(sim_state_t);
    }

  private:
    static constexpr const char *APP_TAG = "SIMT";

    const TickType_t _delay_period = pdMS_TO_TICKS(10 * 7);

    int _w, _h;

    Simulation _simulation;
    Configuration _configuration;
    Display *_display;
    bool _preset_changed;
    bool _overlay_visible;

    void draw_overlay()
    {
        _display->setCursor(2, 2);
        _display->setTextSize(0);
        _display->setTextColor(0x1FF0);
        _display->printf("F:%05.2f%%", _simulation.feed_rate() * 100.0f);
        _display->setCursor(2, 12);
        _display->printf("K:%05.2f%%", _simulation.kill_rate() * 100.0f);
        _display->setCursor(2, 22);

        if (_preset_changed)
        {
            const char *preset = _simulation.name();

            if (strlen(preset) > 0)
            {
                _display->printf("%s", preset);

                return;
            }
        }

        _display->printf("S:%05.2f%%", _simulation.spawn_chance() / 100.0f);
    }
    const int spawn_chances[25] = {
        0,   1,    10,   20,   25,   50,   75,   100,  150,  200,  250,  300,  500,
        750, 1000, 1250, 1500, 1750, 2000, 2500, 3000, 3500, 4000, 4500, 5000,
    };
};

#endif