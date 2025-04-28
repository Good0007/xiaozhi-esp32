#include "iot/thing.h"
#include "board.h"
#include "audio_codec.h"
#include "net_radio.h" 
#include <esp_log.h>

#define TAG "RadioPlayer"

namespace iot {

// 这里仅定义 Speaker 的属性和方法，不包含具体的实现
class RadioPlayer : public Thing {
public:
    RadioPlayer() : Thing("RadioPlayer", "这是一个播放器：可以播放fm网络电台，获取频道列表、频道 ID") {
        // 获取全部频道
        properties_.AddStringProperty("allChannels", "获取全部频道", [this]() -> std::string {
            return radio_.getAllAsString();
        });

       // 按 tag 分类获取频道
        methods_.AddMethod("getChannelsByTag", "按类型获取频道", ParameterList({ 
            Parameter("tag", "频道类型（0:音乐台 1:新闻台 2:综合台）", kValueTypeNumber, true )
        }),[this](const ParameterList& parameters) -> std::string  {
                int tag = parameters["tag"].number();
                std::string channels = radio_.getFmListByTagAsString(tag);
                ESP_LOGI(TAG, "按类型获取频道: %s", channels.c_str());
                return channels;
        });

         // 根据意图（频道名）返回频道 id
         methods_.AddMethod("findChannelId", "根据名称模糊查找频道id", ParameterList({
            Parameter("name", "频道名", kValueTypeString, true )
         }), [this](const ParameterList& parameters) -> std::string {
                std::string name = parameters["name"].string();
                std::string channels = radio_.searchByNameAsString(name);
                ESP_LOGI(TAG, "根据名称模糊查找频道id: %s", channels.c_str());
                return channels;
        });

        // 播放指定频道
        methods_.AddMethod( "playRadio", "播放指定频道",  ParameterList({
            Parameter("fmId", "频道id", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
                uint32_t fm_id = parameters["fmId"].number();
                std::string url = radio_.getFmUrlById(fm_id);
                ESP_LOGI(TAG, "播放指定频道: %" PRIu32, fm_id);
                auto codec = Board::GetInstance().GetAudioCodec();
                codec->play_stream(url.c_str());
                return true;
        });

    }

private:
    NetRadio radio_;
};

} // namespace iot

DECLARE_THING(RadioPlayer);
