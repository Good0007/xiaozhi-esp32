#include "iot/thing.h"
#include "board.h"
#include "sd_audio_reader.h"
#include <esp_log.h>
#include "audio_codec.h"

#define TAG "AudioPlayer"

namespace iot {

// 这里仅定义 AadioPlayer 的属性和方法，不包含具体的实现
class AudioPlayer : public Thing {
public:
    AudioPlayer() : Thing("AudioPlayer", "这是一个mp3播放器") {
        properties_.AddStringProperty("allAudios", "获取全部mp3文件列表", [this]() -> std::string {
            ESP_LOGI(TAG, "获取音乐列表: %s", audio_.getAllAsString().c_str());
            return audio_.getAllAsString();
        });

        // 播放指定的音乐
        methods_.AddMethod( "playAudio", "播放指定的音乐",  ParameterList({
            Parameter("audioName", "文件名称", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
                std::string audioName = parameters["audioName"].string();
                std::string url = audio_.getAudioFilePath(audioName);
                ESP_LOGI(TAG, "播放指定音乐: %s", url.c_str()); // 修正这里
                auto codec = Board::GetInstance().GetAudioCodec();
                codec->play_stream(url.c_str());
                return true;
        });

    }

private:
    SDAudioReader audio_;
};

} // namespace iot

DECLARE_THING(AudioPlayer);
