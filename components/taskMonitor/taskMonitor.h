#include <sys/cdefs.h>

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "table.h"

#define ARRAY_SIZE_OFFSET   5   //Increase this if print_real_time_stats returns ESP_ERR_INVALID_SIZE

static esp_err_t print_real_time_stats(TickType_t xTicksToWait) {

    struct table t;

    table_init(
            &t,
            "Task", "%s",
            "Stack", "%lu",
            "Running Time", "%lu",
            "Percentage", "%lu%%",
            "CPU ID", "%d",
            NULL
    );


    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    uint32_t start_run_time, end_run_time;
    esp_err_t ret;

    //Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    start_array = malloc(sizeof(TaskStatus_t) * start_array_size);
    if (start_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    vTaskDelay(xTicksToWait);

    //Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = malloc(sizeof(TaskStatus_t) * end_array_size);
    if (end_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    //Calculate total_elapsed_time in units of run time stats clock period.
    uint32_t total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ret = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    //Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                //Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        //Check if matching task found
        if (k >= 0) {
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * portNUM_PROCESSORS);
            table_add(&t, start_array[i].pcTaskName, end_array[i].usStackHighWaterMark, task_elapsed_time,
                      percentage_time,(start_array[i].xCoreID >=0 && start_array[i].xCoreID <100)?start_array[i].xCoreID:-1);
        }
    }
    table_print(&t, 100, stdout);
    table_free(&t);
    //Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            printf("a task <%s> Deleted\n", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            printf("a task <%s> Created\n", end_array[i].pcTaskName);
        }
    }
    ret = ESP_OK;

    exit:    //Common return path
    free(start_array);
    free(end_array);
    return ret;
}

_Noreturn
static
void stats_task(void *param) {
    //Print real time stats periodically
    uint32_t ticks = pdMS_TO_TICKS((uint32_t) param);
    while (1) {
        print_real_time_stats(ticks);
    }
}

void startTaskMonitor(uint32_t StatisticsPeriod_ms) {
    xTaskCreate(stats_task, "TaskMonitor", 3 * 1024, (void *) StatisticsPeriod_ms, 5, NULL);
}