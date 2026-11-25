#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "nvs.h"
#include "nvs_flash.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>
#include <string.h>

#define CONFIGURATION_SAVE_DELAY (10LL * 1000LL * 1000LL)

class Configuration
{
  public:
    Configuration(const char *name, size_t config_size)
    {
        strncpy(_name, name, 31);

        _config_size = config_size;
        _is_dirty = false;

        register_configuration(this);
    }

    void init()
    {
        _write_buffer = heap_caps_malloc(_config_size, MALLOC_CAP_INTERNAL);
    }

    inline uint64_t last_edit()
    {
        return _last_edit;
    }

    esp_err_t save(const void *config)
    {
        return save(config, CONFIGURATION_SAVE_DELAY);
    }

    esp_err_t save(const void *config, uint64_t save_after)
    {
        memcpy(_write_buffer, config, _config_size);

        _last_edit = esp_timer_get_time();
        _next_save = _last_edit + save_after;

        return ESP_OK;
    }

    esp_err_t load(void *config)
    {
        nvs_handle_t handle;
        esp_err_t err = nvs_open("settings", NVS_READONLY, &handle);
        if (err != ESP_OK)
        {
            return err;
        }

        size_t len = _config_size;

        err = nvs_get_blob(handle, _name, config, &len);

        nvs_close(handle);

        return err;
    }

  private:
    char _name[32];
    uint64_t _next_save;
    uint64_t _last_edit;
    size_t _config_size;
    bool _is_dirty;
    void *_write_buffer;

    static void register_configuration(Configuration *configuration);
    static void flush();

    static Configuration *_configurations[10];
    static int _config_count;

    friend void start_config_task();
    friend void configuration_writer_task(void *);

    esp_err_t save_if_dirty(uint64_t now)
    {
        if ((_next_save == 0LL) || (now < _next_save))
        {
            return ESP_OK;
        }

        nvs_handle_t handle;
        esp_err_t err = nvs_open("settings", NVS_READWRITE, &handle);

        if (err != ESP_OK)
        {
            return err;
        }

        err = nvs_set_blob(handle, _name, _write_buffer, _config_size);

        if (err == ESP_OK)
        {
            err = nvs_commit(handle);
        }

        _next_save = 0LL;

        nvs_close(handle);
        return err;
    }
};

void start_config_task();

#endif