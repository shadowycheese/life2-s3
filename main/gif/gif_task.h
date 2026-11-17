#ifndef GIF_TASK_H
#define GIF_TASK_H

#include "gif/gif_player.h"
#include "gif/sample/sf.h"
#include "gif/sample/test.h"
#include "gif/sample/test2.h"
#include "matrix/display.h"
#include "matrix/hub75_driver.h"
#include "task.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>

typedef struct
{
    uint8_t *gif;
    size_t *gif_size;
    char name[32];
} gif_info_t;

class GIFTask : public Task
{
  public:
    GIFTask(Display *display)
    {
        _display = display;
    }

    virtual void init()
    {
        gifplayer.init(_display);
        gifplayer.openData(__236_spintriangle_gif, __236_spintriangle_gif_len);
    }

    virtual bool run(bool overlay_visible, uint64_t now, TickType_t *sleep_time)
    {
        int delay = gifplayer.play();

        *sleep_time = pdMS_TO_TICKS(delay);

        return false;
    }

    virtual const char *configuration_name()
    {
        return "gif_config";
    }

    virtual size_t get_configuration_size()
    {
        return sizeof(gif_info_t);
    }

    virtual size_t get_configuration(void *buffer)
    {
        return sizeof(gif_info_t);
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

    GIFPlayer gifplayer;
    Display *_display;
};

#endif