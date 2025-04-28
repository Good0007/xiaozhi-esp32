#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include "sd_audio_reader.h"
#include "application.h"
#include "system_info.h"

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    SDAudioReader reader;
    if (!reader.initialize()) {
        ESP_LOGE("MAIN", "SD card initialization failed");
        return;
    }

    reader.listAudioFiles();
    int count = reader.getAudioFileCount();
    ESP_LOGI("MAIN", "Found %d audio files:", count);

    const auto& files = reader.getAudioFiles();
    for (const auto& name : files) {
        ESP_LOGI("MAIN", "Audio file: %s", name.c_str());
    }

    // Launch the application
    Application::GetInstance().Start();
}
