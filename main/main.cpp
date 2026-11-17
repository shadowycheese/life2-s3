#include "driver/gpio.h"
#include "gif/gif_task.h"
#include "io/bluetooth.h"
#include "io/encoder.h"
#include "io/espio.h"
#include "matrix/display.h"
#include "matrix/hub75_driver.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "simulation/sim_task.h"
#include "simulation/simulation.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>

Hub75Driver hub75_driver;
Display display(&hub75_driver);
SimTask sim_task(&display);
GIFTask gif_task(&display);
Bluetooth bt;
uint64_t overlay_expiry;
bool preset_changed;
uint64_t save_configuration_time;
Task *current_task;

const char *APP_TAG = "APP";

#define TICK_SIZE (10)

#define OVERLAY_TIME (1500LL * 1000LL)
#define CONFIGURATION_SAVE_DELAY (10LL * 1000LL * 1000LL)

esp_err_t save_configuration(Task *task)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("settings", NVS_READWRITE, &handle);

    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t buffer[256];
    size_t len = task->get_configuration(buffer);

    err = nvs_set_blob(handle, "config", buffer, len);

    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t load_configuration(Task *task)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("settings", NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t buffer[256];
    size_t len = task->get_configuration_size();

    err = nvs_get_blob(handle, "config", buffer, &len);

    task->set_configuration(buffer);

    nvs_close(handle);
    return err;
}

void configuration_writer(void *)
{
    for (;;)
    {
        uint64_t time = esp_timer_get_time();

        if (save_configuration_time > 0 && save_configuration_time < time)
        {
            save_configuration_time = 0;

            esp_err_t err = save_configuration(current_task);

            if (err == ESP_OK)
            {
                ESP_LOGI(APP_TAG, "Saved config OK");
            }
            else
            {
                ESP_LOGW(APP_TAG, "Failed to save config: %d", err);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void configuration_init()
{
    load_configuration(current_task);

    save_configuration_time = 0;

    xTaskCreatePinnedToCore(configuration_writer, "config_writer", 4096, NULL, 1, NULL, 1);
}

void check_bt(uint64_t now)
{
    int16_t value;

    if (bt.feed_rate_updated(&value))
    {
        float v = value / 10000.0f;

        // sim.set_feed_rate(v);

        ESP_LOGI(APP_TAG, "Feed Rate: [%d] %0.4f%%", value, v);

        overlay_expiry = now + OVERLAY_TIME;
        save_configuration_time = now + CONFIGURATION_SAVE_DELAY;
    }

    if (bt.kill_rate_updated(&value))
    {
        float v = value / 10000.0f;

        // sim.set_kill_rate(v);

        ESP_LOGI(APP_TAG, "Kill Rate: [%d] %0.4f%%", value, v);

        overlay_expiry = now + OVERLAY_TIME;
        save_configuration_time = now + CONFIGURATION_SAVE_DELAY;
    }

    if (bt.spawn_chance_updated(&value))
    {
        // sim.set_spawn_chance(value);

        ESP_LOGI(APP_TAG, "Spawn Chance: [%d] %0.2f", value, value / 100.0f);

        overlay_expiry = now + OVERLAY_TIME;
        save_configuration_time = now + CONFIGURATION_SAVE_DELAY;
        preset_changed = false;
    }

    if (bt.preset_updated(&value))
    {
        // sim.set_preset(value);

        ESP_LOGI(APP_TAG, "Selected Preset: %d", value);

        overlay_expiry = now + OVERLAY_TIME;
        save_configuration_time = now + CONFIGURATION_SAVE_DELAY;
        preset_changed = true;
    }

    if (bt.add_command(&value))
    {
        int16_t x = (value >> 8) & 0xFF;
        int16_t y = value & 0xFF;
        // sim.add(x, y);

        ESP_LOGI(APP_TAG, "Add: %d, %d", x, y);
    }

    if (bt.hue_command())
    {
        // sim.next_hue();

        ESP_LOGI(APP_TAG, "Next hue");

        save_configuration_time = now + CONFIGURATION_SAVE_DELAY;
    }

    if (bt.reset_command())
    {
        // sim.reset();

        ESP_LOGI(APP_TAG, "Reset");
    }
}

void clear_screen(int active)
{
    if (active)
    {
        display.clear_screen();
    }
}

void main_task(void *)
{
    ESP_LOGI(APP_TAG, "Running main loop on core %d", xPortGetCoreID());

    TickType_t last_wake_time = xTaskGetTickCount();
    TickType_t sleep_time;

    for (int i = 0;; i++)
    {
        uint64_t now = esp_timer_get_time();

        // check_bt(now);

        bool sleep_until = current_task->run(now < overlay_expiry, now, &sleep_time);

        display.commit_frame();

        if (sleep_until)
        {
            vTaskDelayUntil(&last_wake_time, sleep_time);
        }
        else
        {
            vTaskDelay(sleep_time);
        }
    }
}

void dump_memory(const char *title)
{
    printf("\n============== %s ============\n", title);
    printf("Internal: %u (%u)\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    printf("DMA:      %u\n", heap_caps_get_free_size(MALLOC_CAP_DMA));
    printf("PSRAM:    %u\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    printf("8-bit:    %u\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

extern "C" void app_main(void)
{
    nvs_flash_init();

    // bt.init(NULL);

    hub75_driver.init();

    display.init();

    gif_task.init();
    sim_task.init();

    current_task = &gif_task;

    configuration_init();

    overlay_expiry = esp_timer_get_time() + 2000000LL;

    xTaskCreatePinnedToCore(main_task, "main_task", 4096, NULL, 5, NULL, 1);
}
