#pragma once
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class OtaUtils {
    public:
        static void print_task_stats() {
            char buffer[1024];
            //vTaskList(buffer);
            //printf("Task Name\tState\tPrio\tStack\tNum\tCore\n%s\n", buffer);
            vTaskGetRunTimeStats(buffer);
            printf("Task Name\tTime\tPercent\n%s\n", buffer);
        }
        static void SwitchToOtherApp(Display* display = nullptr) {
            const esp_partition_t *running = esp_ota_get_running_partition();
            const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
            if (target != nullptr) {
                ESP_LOGI("OTA", "当前运行分区: %s (0x%lx)", running->label, running->address);
                ESP_LOGI("OTA", "切换至分区: %s (0x%lx)", target->label, target->address);
                if (display) {
                    //char message[100];
                    //snprintf(message, sizeof(message), "切换至分区: %s", target->label);
                    //display->SetChatMessage("system", message);
                    //display->ShowNotification(message, 3000);
                    display->SetChatMessage("system", "即将切到网络收音机...");
                }
                esp_err_t err = esp_ota_set_boot_partition(target);
                if (err == ESP_OK) {
                    ESP_LOGI("OTA", "分区切换成功，准备重启...");
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    //重启
                    esp_restart();
                } else {
                    ESP_LOGE("OTA", "设置启动分区失败: %s", esp_err_to_name(err));
                    if (display) {
                        display->SetChatMessage("system", "系统切换失败!");
                    }
                }
            } else {
                ESP_LOGE("OTA", "未找到可切换的分区");
                if (display) {
                    display->SetChatMessage("system", "未找到分区!");
                }
            }
        }
    };