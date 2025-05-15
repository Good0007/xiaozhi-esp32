#include "iot/thing.h"
#include "board.h"
#include "audio_codec.h"
#include "net_radio.h" 
#include <esp_log.h>
#include "application.h"

#define TAG "RadioPlayer"

namespace iot {

// 这里仅定义 Speaker 的属性和方法，不包含具体的实现
class RadioPlayer : public Thing {
public:
    RadioPlayer() : Thing("RadioPlayer", "这是一个网络收音机，可以获取频道，播放指定频道") {
        // 获取全部频道
        properties_.AddStringProperty("state", "播放器状态：0 空闲 1 播放中", [this]() -> std::string {
            auto& app = Application::GetInstance();
            std::string state_ = (app.GetPlayingType() == PlayingType::Mp3Stream ? "1" : "0");
            ESP_LOGI(TAG, "获取播放状态状态: %s", state_.c_str());
            return state_;
        });

       // 按 tag 分类获取频道
        methods_.AddMethod("getChannelsByTag", "按类型获取频道", ParameterList({ 
            Parameter("tag", "频道类型（0:音乐 1:资讯 2:交通 3:财经 4:全部）", kValueTypeNumber, true )
        }),[this](const ParameterList& parameters) -> std::string  {
                std::string channels = "";
                int tag = parameters["tag"].number();
                if (tag > 3) {
                    // 获取全部频道
                    channels = radio_.getAllAsString();
                } else {
                    // 获取指定类型的频道
                    channels = radio_.getFmListByTagAsString(tag);
                }
                ESP_LOGI(TAG, "按类型获取频道: %d = %s",tag, channels.c_str());
                return channels;
        });

         // 根据意图（频道名）返回频道 id
         methods_.AddMethod("playRadio", "播放指定名称的电台", ParameterList({
            Parameter("name", "电台频道名称", kValueTypeString, true )
         }), [this](const ParameterList& parameters) {
                std::string name = parameters["name"].string();
                //std::string channels = radio_.searchByNameAsString(name);
                ESP_LOGI(TAG, "根据名称模糊查找频道: %s", name.c_str());
                auto& app = Application::GetInstance();
                PlayInfo play_info_ = radio_.getPlayInfo(name); 
                ESP_LOGI(TAG, "根据名称模糊查找频道id: name = %s, url = %s",play_info_.name.c_str(), play_info_.url.c_str());
                //传入list集合
                std::vector<PlayInfo> play_list{play_info_};
                app.ChangePlaying(PlayingType::Mp3Stream, play_list);
                // 在主线程上下文执行播放操作
                //app.PlayMp3Stream();
                //return true;
                //return channels;
        });
    }

private:
    NetRadio radio_;
    std::string last_url_; // 新增成员变量
};

} // namespace iot

DECLARE_THING(RadioPlayer);
