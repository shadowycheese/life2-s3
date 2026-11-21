#include "jobs/dispatcher.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

QueueHandle_t _job_queue = NULL;

static const char *DISPATCH_TAG = "JOB";

void dispatcher_task(void *pvParameters)
{
    job_t *job;

    for (;;)
    {
        // Block indefinitely until ANY producer posts a job to the queue
        if (xQueueReceive(_job_queue, &job, portMAX_DELAY) == pdTRUE)
        {
            (*job)();

            delete job;
        }
    }
}

extern "C"
{
    void dispatch_job(job_t job)
    {
        auto *p = new job_t(std::move(job));

        xQueueSend(_job_queue, &p, portMAX_DELAY);
    }

    void dispatcher_init()
    {
        ESP_LOGI(DISPATCH_TAG, "Starting EDT...");

        _job_queue = xQueueCreate(128, sizeof(job_t *));

        xTaskCreatePinnedToCore(dispatcher_task, "dispatcher", 8192, NULL, 2, NULL, 1);
    }
}
