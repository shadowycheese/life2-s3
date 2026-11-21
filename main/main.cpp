#include "bt/bluetooth.h"
#include "common.h"
#include "config/config.h"
#include "driver/gpio.h"
#include "esp_littlefs.h"
#include "gif/gif_task.h"
#include "jobs/dispatcher.h"
#include "mandlebrot/mand_task.h"
#include "matrix/display.h"
#include "matrix/hub75_driver.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "simulation/sim_task.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>

#include <dirent.h>
#include <sys/stat.h>

typedef struct
{
    display_mode_t mode;
} global_configuration_t;

global_configuration_t global_config;
Configuration global_configuration("global_config", sizeof(global_configuration_t));

Hub75Driver hub75_driver;
Display display(&hub75_driver);

SimTask sim_task(&display);
GIFTask gif_task(&display);
MandlebrotTask mand_task(&display);
Bluetooth bt;
uint64_t save_configuration_time;
Task *current_task;
mode_t current_mode;

const char *APP_TAG = "APP";

#define COMMAND_MODE 0x7E

void main_task(void *)
{
    ESP_LOGI(APP_TAG, "Running main loop on core %d", xPortGetCoreID());

    TickType_t last_wake_time = xTaskGetTickCount();
    TickType_t sleep_time;

    for (int i = 0;; i++)
    {
        uint64_t now = esp_timer_get_time();

        bool sleep_until = current_task->run(now, &sleep_time);

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
    printf("Internal: %u (%u)\n",
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    printf("DMA: %u (%u)\n", heap_caps_get_free_size(MALLOC_CAP_DMA), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    printf("PSRAM: %u (%u)\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    printf("8-bit:: %u (%u)\n", heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

extern "C" void set_display_mode(display_mode_t mode)
{
    mode = (display_mode_t)((uint8_t)mode % 3);

    bool save = mode != global_config.mode;

    global_config.mode = mode;

    switch (global_config.mode)
    {
    case 0:
        current_task = &sim_task;
        break;
    case 1:
        current_task = &gif_task;
        break;
    case 2:
        current_task = &mand_task;
        break;
    }

    if (save)
    {
        global_configuration.save(&global_config);
    }
}

void list_files()
{
    DIR *dir = opendir("/vol");

    if (!dir)
        return;

    ESP_LOGI(APP_TAG, "Listing files");
    struct dirent *entry;

    int files = 0;
    while ((entry = readdir(dir)) != nullptr)
    {
        struct stat st;
        char path[300];

        snprintf(path, 300, "/vol/%s", entry->d_name);

        if (stat(path, &st) == 0)
        {
            files++;

            ESP_LOGI(APP_TAG, "%s (%ld bytes)", entry->d_name, (long)st.st_size);
        }
    }

    closedir(dir);
    ESP_LOGI(APP_TAG, "Found: %d", files);
}

void init_fs()
{
    ESP_LOGI(APP_TAG, "Starting LittleFS");

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = "/vol";
    conf.partition_label = "storage";
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;

    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));

    list_files();
}

extern "C" void app_main(void)
{
    nvs_flash_init();

    init_fs();

    bt.init();

    register_bluetooth_event_handler(COMMAND_MODE,
                                     [](bluetooth_event_t *event)
                                     {
                                         if (event->op == BLE_GATT_ACCESS_OP_READ_CHR)
                                         {
                                             event->send_size = 1;
                                             event->send_buffer[0] = (uint8_t)global_config.mode;
                                         }
                                         else
                                         {
                                             set_display_mode((display_mode_t)(event->recv_buffer[0]));
                                         }
                                     });

    dispatcher_init();

    hub75_driver.init();

    display.init();

    gif_task.init();
    sim_task.init();
    mand_task.init();

    global_configuration.load(&global_config);

    set_display_mode(global_config.mode);

    start_config_task();

    dump_memory("init");

    xTaskCreatePinnedToCore(main_task, "main_task", 4096, NULL, 5, NULL, 1);
}
