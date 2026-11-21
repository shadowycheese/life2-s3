#ifndef GIF_TASK_H
#define GIF_TASK_H

#include "bt/bluetooth.h"
#include "common.h"
#include "esp_littlefs.h"
#include "gif/gif_player.h"
#include "jobs/dispatcher.h"
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

#define COMMAND_GIF_START 0x50
#define COMMAND_GIF_BLOCK 0x51
#define COMMAND_GIF_ABORT 0x52
#define COMMAND_GIF_END 0x53

#define BLOCK_SIZE 250
#define WRITE_SIZE (1024 * 16)

#define GIF_MAGIC 0x61F5ACED

typedef struct
{
    char name[32];
    uint32_t magic;
} gif_configuration_t;

class GIFTask : public Task
{
  public:
    GIFTask(Display *display) : _configuration("gif_config", sizeof(gif_configuration_t))
    {
        _display = display;
    }

    virtual void init()
    {
        _gif_player.init(_display);
        _save_buffer = (uint8_t *)heap_caps_aligned_alloc(16, 1024 * 1024, MALLOC_CAP_SPIRAM);
        _configuration.set_save_delay(1LL);
        _configuration.load(&_config);
        _playing_gif = NULL;

        register_bluetooth_event_handler(COMMAND_GIF_START,
                                         [this](bluetooth_event_t *event)
                                         {
                                             _upload_size = 0;
                                             _upload_progress = 0;
                                             _uploading = true;

                                             for (int i = 0; i < 4; i++)
                                             {
                                                 _upload_size = (_upload_size << 8) + event->recv_buffer[i];
                                             }

                                             strncpy(_uploading_file_name, (char *)&(event->recv_buffer[4]), 31);

                                             event->send_buffer[0] = 0x00;
                                             event->send_buffer[1] = (BLOCK_SIZE >> 8) & 0xFF;
                                             event->send_buffer[2] = (BLOCK_SIZE) & 0xFF;
                                             event->send_buffer[3] = 0x08;
                                             event->send_size = 4;

                                             ESP_LOGI(GIF_TAG, "Start to upload %s [%d]", _uploading_file_name, _upload_size);

                                             set_display_mode(MODE_GIF);
                                         });

        register_bluetooth_event_handler(COMMAND_GIF_BLOCK,
                                         [this](bluetooth_event_t *event)
                                         {
                                             uint16_t seq = 0;
                                             for (int i = 0; i < 2; i++)
                                             {
                                                 seq = (seq << 8) + event->recv_buffer[i];
                                             }

                                             int len = event->recv_size - 2;

                                             _upload_progress += len;

                                             memcpy(_save_buffer + (seq * BLOCK_SIZE), &(event->recv_buffer[2]), len);

                                             event->send_buffer[0] = event->recv_buffer[0];
                                             event->send_buffer[1] = event->recv_buffer[1];
                                             event->send_buffer[2] = 0x00;
                                             event->send_size = 3;
                                         });
        register_bluetooth_event_handler(COMMAND_GIF_ABORT,
                                         [this](bluetooth_event_t *event)
                                         {
                                             _upload_progress = 0;
                                             _upload_size = 0;
                                             _uploading = false;
                                             _uploading_file_name[0] = 0;

                                             event->send_buffer[0] = 0x00;
                                             event->send_size = 1;
                                         });
        register_bluetooth_event_handler(COMMAND_GIF_END,
                                         [this](bluetooth_event_t *event)
                                         {
                                             uint16_t crc = 0;
                                             for (int i = 0; i < 2; i++)
                                             {
                                                 crc = (crc << 8) + event->recv_buffer[i];
                                             }

                                             event->send_buffer[0] = 0x00;
                                             event->send_size = 1;

                                             dispatch_job(
                                                 [this]
                                                 {
                                                     save_gif();

                                                     start_gif();

                                                     _uploading = false;
                                                 });
                                         });

        start_gif();
    }

    virtual bool run(uint64_t now, TickType_t *sleep_time)
    {
        if (_uploading)
        {
            _display->clear_screen();

            _display->setCursor(4, 4);
            _display->printf("%0.2f", (float)_upload_progress / (float)_upload_size);

            *sleep_time = pdMS_TO_TICKS(50);
        }
        else if (_playing_gif)
        {
            int delay = _gif_player.play();

            *sleep_time = pdMS_TO_TICKS(delay);
        }
        else
        {
            _display->clear_screen();

            _display->setCursor(4, 4);
            _display->printf("No GIF");

            *sleep_time = pdMS_TO_TICKS(150);
        }

        return false;
    }

  private:
    static constexpr const char *GIF_TAG = "GIFT";

    gif_configuration_t _config;
    char _uploading_file_name[32];
    uint32_t _upload_size;
    uint32_t _upload_progress;
    bool _uploading;
    uint8_t *_save_buffer;
    FILE *_playing_gif;

    Configuration _configuration;
    GIFPlayer _gif_player;
    Display *_display;

    const char *_temp_file_name = VOLUME_ROOT "/new.gif";
    const char *_playing_file_name = VOLUME_ROOT "/current.gif";

    void save_gif()
    {
        FILE *tmp = fopen(_temp_file_name, "w");

        ESP_LOGI(GIF_TAG, "Saving file: %s=>%s [%d]", _uploading_file_name, _temp_file_name, _upload_size);

        for (int i = 0; i < _upload_size; i += WRITE_SIZE)
        {
            int block_size = (i + WRITE_SIZE) <= _upload_size ? WRITE_SIZE : _upload_size - i;

            fwrite(&_save_buffer[i], block_size, sizeof(uint8_t), tmp);

            vTaskDelay(1);
        }

        fclose(tmp);
        ESP_LOGI(GIF_TAG, "File saved: %s [%d]", _temp_file_name, _upload_size);

        close_playing_gif();

        int rc = remove(_playing_file_name);
        ESP_LOGI(GIF_TAG, "Deleted old file: %s [%d]", _playing_file_name, rc);
        rc = rename(_temp_file_name, _playing_file_name);
        ESP_LOGI(GIF_TAG, "Renaming file: %s=>%s", _temp_file_name, _playing_file_name, rc);

        strcpy(_config.name, _uploading_file_name);

        _config.magic = GIF_MAGIC;

        _configuration.save(&_config);
        ESP_LOGI(GIF_TAG, "Saved config: %s", _config.name);
    }

    void start_gif()
    {
        close_playing_gif();

        if (_config.magic != GIF_MAGIC)
        {
            return;
        }

        if (_config.name[0] == 0)
        {
            return;
        }

        struct stat st;

        if (stat(_playing_file_name, &st) != 0)
        {
            return;
        }

        _playing_gif = fopen(_playing_file_name, "r");

        if (_playing_gif)
        {
            _gif_player.close();
            ESP_LOGI(GIF_TAG, "Starting GIF:[size: %d] [handle: %08X]", st.st_size, _playing_gif);
            _gif_player.open_file(_playing_gif, st.st_size);
        }
    }

    void close_playing_gif()
    {
        if (_playing_gif)
        {
            fclose(_playing_gif);

            _playing_gif = nullptr;
        }
    }
};

#endif