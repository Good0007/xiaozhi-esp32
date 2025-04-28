#ifndef SD_AUDIO_READER_H
#define SD_AUDIO_READER_H

// 定义 SD 卡的 GPIO 引脚
#define SDMMC_CS_PIN GPIO_NUM_10
#define SDMMC_MOSI_PIN GPIO_NUM_11
#define SDMMC_CLK_PIN GPIO_NUM_12
#define SDMMC_MISO_PIN GPIO_NUM_13  // DAT0
#define SDMMC_CD_PIN GPIO_NUM_38
#define MOUNT_POINT "/sdcard"

#include <vector>
#include <string>

class SDAudioReader {
public:
    SDAudioReader();
    ~SDAudioReader() = default;

    bool initialize();
    void listAudioFiles();
    int getAudioFileCount() const;
    const std::vector<std::string>& getAudioFiles() const;

    //获取全部音频文件列表，转换为字符串
    std::string getAllAsString() const {
        std::string result;
        for (const auto& file : audioFiles) {
            result += file + "\n";
        }
        return result;
    }

    //获取指定音频文件的路径
    std::string getAudioFilePath(std::string name) const {
        std::string path = MOUNT_POINT;
        path += "/";
        path += name;
        return path;
    }

private:
    std::vector<std::string> audioFiles;
};

#endif // SD_AUDIO_READER_H