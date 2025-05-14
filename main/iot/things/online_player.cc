#include "iot/thing.h"
#include "board.h"
#include <esp_log.h>
#include "audio_codec.h"
#include "application.h"
#include "online_music.h"
#define TAG "OnlineMp3Player"

namespace iot {

// 这里仅定义 AadioPlayer 的属性和方法，不包含具体的实现
class OnlineMp3Player : public Thing {
public:
    OnlineMp3Player() : Thing("OnlineMp3Player", "这是一个在线mp3播放器，可以搜索在线音乐进行播放") {
        properties_.AddStringProperty("mp3State", "播放器状态：0 空闲 1 播放中", [this]() -> std::string {
            auto& app = Application::GetInstance();
            std::string state_ = (app.GetPlayingType() == PlayingType::Mp3Stream ? "1" : "0");
            ESP_LOGI(TAG, "获取播放状态状态: %s", state_.c_str());
            return state_;
        });

        // 播放指定的音乐
        methods_.AddMethod("playMp3", "搜索音乐进行播放",  ParameterList({
            Parameter("keyword", "搜索关键字：可以是音乐名称/歌手等", kValueTypeString, true)
        }), [this](const ParameterList& parameters) -> std::string {
                std::string keyword = parameters["keyword"].string();
                ESP_LOGI(TAG, "搜索音乐: %s", keyword.c_str());
                PlayInfo play_info = MusicSearch::getRandomPlayInfo(keyword);
                if (!play_info.url.empty()) {
                    auto& app = Application::GetInstance();
                    ESP_LOGI(TAG, "根据名称搜索歌曲: name = %s, url = %s",play_info.name.c_str(), play_info.url.c_str());
                    app.changePlaying(PlayingType::Mp3Stream, play_info);
                    return "播放成功";
                } else {
                    ESP_LOGI(TAG, "No results found for keyword: %s", keyword.c_str());
                }
                return "没有找到结果";
        });

    }

private:
};

} // namespace iot

DECLARE_THING(OnlineMp3Player);
