#include "sd_audio_reader.h"
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_log.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <driver/gpio.h>

static const char *TAG = "SDAudioReader";

#ifndef ESP_VFS_FAT_FLAG_UTF8
#define ESP_VFS_FAT_FLAG_UTF8 0
#endif
#ifndef ESP_VFS_FAT_FLAG_LONG_FILENAMES
#define ESP_VFS_FAT_FLAG_LONG_FILENAMES 0
#endif

SDAudioReader::SDAudioReader() {}

bool SDAudioReader::initialize()
{
    ESP_LOGI(TAG, "Using Spi peripheral");
    esp_err_t ret;
    sdmmc_card_t *card;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SDMMC_MOSI_PIN,
        .miso_io_num = SDMMC_MISO_PIN,
        .sclk_io_num = SDMMC_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize( (spi_host_device_t) host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return false;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false
     };


    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SDMMC_CS_PIN;
    slot_config.host_id =  (spi_host_device_t) host.slot;

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "SD card mounted");
    //列出文件
    listAudioFiles();
    return true;
}

void SDAudioReader::listAudioFiles()
{
    audioFiles.clear();
    DIR *dir = opendir(MOUNT_POINT);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open /sdcard directory");
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name(entry->d_name);
        ESP_LOGE(TAG, "Found file: %s", name.c_str());
        if (name.length() > 4) {
            std::string ext = name.substr(name.length() - 4);
            if (ext == ".mp3") {
                audioFiles.push_back(name);
            }
        }
    }
    closedir(dir);
}

int SDAudioReader::getAudioFileCount() const
{
    return audioFiles.size();
}

const std::vector<std::string> &SDAudioReader::getAudioFiles() const
{
    if (audioFiles.empty()) {
        ESP_LOGI(TAG, "Not Found audio files");
    }
    return audioFiles;
}