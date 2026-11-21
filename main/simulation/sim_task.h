#ifndef SIM_TASK_H
#define SIM_TASK_H

#include "matrix/display.h"
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

        register_bluetooth_event_handler(COMMAND_STATE,
                                         [this](bluetooth_event_t *event)
                                         {
                                             if (event->op == BLE_GATT_ACCESS_OP_READ_CHR)
                                             {
                                                 event->send_size = get_state(event->send_buffer);
                                             }
                                         });
        register_bluetooth_event_handler(COMMAND_FEED_RATE,
                                         [this](bluetooth_event_t *event)
                                         {
                                             _simulation.set_feed_rate(bt_valuef(event, 10000));

                                             save_configuration();
                                         });
        register_bluetooth_event_handler(COMMAND_KILL_RATE,
                                         [this](bluetooth_event_t *event)
                                         {
                                             _simulation.set_kill_rate(bt_valuef(event, 10000));

                                             save_configuration();
                                         });
        register_bluetooth_event_handler(COMMAND_SPAWN_CHANCE,
                                         [this](bluetooth_event_t *event)
                                         {
                                             _simulation.set_spawn_chance(bt_valuef(event, 100));

                                             save_configuration();
                                         });
        register_bluetooth_event_handler(COMMAND_PRESET,
                                         [this](bluetooth_event_t *event)
                                         {
                                             _simulation.set_preset(bt_value(event));

                                             save_configuration();
                                         });
        register_bluetooth_event_handler(COMMAND_RESET,
                                         [this](bluetooth_event_t *event)
                                         {
                                             _simulation.reset(); //
                                         });
        register_bluetooth_event_handler(COMMAND_ADD,
                                         [this](bluetooth_event_t *event)
                                         {
                                             if (event->recv_size >= 2)
                                             {
                                                 _simulation.add(event->recv_buffer[0], event->recv_buffer[1]);
                                             }
                                         });
        register_bluetooth_event_handler(COMMAND_HUE,
                                         [this](bluetooth_event_t *event)
                                         {
                                             _simulation.next_hue();

                                             save_configuration();
                                         });
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

  private:
    static constexpr const char *APP_TAG = "SIMT";

    const TickType_t _delay_period = pdMS_TO_TICKS(10 * 5);

    int _w, _h;

    Simulation _simulation;
    Configuration _configuration;
    Display *_display;
    bool _preset_changed;
    bool _overlay_visible;

    void save_configuration()
    {
        sim_configuration_t config;

        _simulation.get_configuration(&config);

        _configuration.save(&config);
    }

    float bt_valuef(bluetooth_event_t *event, int16_t scale)
    {
        int16_t value = bt_value(event);

        return (float)value / (float)scale;
    }

    int16_t bt_value(bluetooth_event_t *event)
    {
        if (event->recv_size > 0)
        {
            int16_t value = event->recv_buffer[0];

            if (event->recv_size > 0)
            {
                value *= 256;
                value += event->recv_buffer[1];
            }

            return value;
        }

        return 0;
    }

    uint16_t get_state(void *buffer)
    {
        sim_state_t *state = (sim_state_t *)buffer;

        state->feed_rate = _simulation.feed_rate();
        state->kill_rate = _simulation.kill_rate();
        state->preset = _simulation.preset();
        state->spawn_chance = _simulation.spawn_chance();

        return (uint16_t)sizeof(sim_state_t);
    }

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