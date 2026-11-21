#include "config/config.h"

Configuration *Configuration::_configurations[10];
int Configuration::_config_count;

void Configuration::register_configuration(Configuration *configuration)
{
    _configurations[_config_count] = configuration;

    _config_count++;
}

void Configuration::flush()
{
    uint64_t now = esp_timer_get_time();

    for (int i = 0; i < _config_count; i++)
    {
        _configurations[i]->save_if_dirty(now);
    }
}

void configuration_writer_task(void *)
{
    for (;;)
    {
        Configuration::flush();

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void start_config_task()
{
    for (int i = 0; i < Configuration::_config_count; i++)
    {
        Configuration::_configurations[i]->init();
    }

    xTaskCreatePinnedToCore(configuration_writer_task, "config_writer", 4096, NULL, 1, NULL, 1);
}