#include "iot/thing.h"
#include "board.h"
#include "display/display.h"
#include <esp_log.h>
#include "audio_codec.h"
#include "application.h"
#include "sd_audio_reader.h"
#define TAG "LocalMp3Player"

namespace iot {

class LocalMp3Player : public Thing {
public:
    LocalMp3Player() : Thing("LocalMp3Player", "本地音乐库：支持搜索音乐、指定歌曲名称|歌手 播放本地sd卡里的音乐（不要使用search_music工具！）") {
        properties_.AddStringProperty("localPlayState", "播放器状态：0 空闲 1 播放中", [this]() -> std::string {
            auto& app = Application::GetInstance();
            std::string state_ = (app.GetPlayingType() == PlayingType::LocalAudio ? "1" : "0");
            ESP_LOGI(TAG, "sdcardState: %s", state_.c_str());
            return state_;
        });

         properties_.AddNumberProperty("sdcardMusicCount", "本地sd卡音乐数量", [this]() -> int {
            int count = Board::GetInstance().GetSDAudioReader()->getAudioFileCount();
            return count;
        });

        properties_.AddStringProperty("localPlayList", "获取本地播放列表信息", [this]() -> std::string {
            std::string first_name = "";
            int count = 0;
            auto& app = Application::GetInstance();
            std::vector<PlayInfo> play_list_ = app.GetPlayList();
            if (play_list_.empty()) {
                return "当前列表没有本地音乐";
            }
            for (const auto& info : play_list_) {
                if (info.type == PlayingType::LocalAudio) {
                    count++;
                    if (first_name.empty()) {
                        first_name = info.name;
                    }
                }
            }
            if (count == 0) {
                return "当前列表没有本地音乐";
            }
            ESP_LOGI(TAG, "localPlayList: count = %d, first_name = %s",count, first_name.c_str());
            return std::to_string(count)+"首加入到播放列表,第一首:" + first_name;
        });

        methods_.AddMethod("localMusicSearch", "搜索本地音乐",  ParameterList({
            Parameter("keyword", "搜索关键字：可以是音乐名称/歌手", kValueTypeString, true),
            Parameter("count", "搜索数量：默认10 (如果用户明确搜索指定歌曲，就传入1)", kValueTypeNumber, false),
        }), [this](const ParameterList& parameters) -> std::string {
                std::string keyword = parameters["keyword"].string();
                int count = parameters["count"].number();
                if (count <= 0 && count > 20) {
                    count = 10;
                }
                ESP_LOGI(TAG, "localMusicSearch: %s,count = %d", keyword.c_str(), count);
                std::vector<std::string> misuc_list_ = Board::GetInstance().
                        GetSDAudioReader()->searchPlayList(keyword, count);
                if (misuc_list_.empty()) {
                    ESP_LOGI(TAG, "localMusicSearch: 没有找到结果");
                    return "没有找到结果";
                }
                //循环搜索结果，拼接成一个List结果
                ESP_LOGI(TAG, "localMusicSearch: 找到 %s 首音乐！", std::to_string(misuc_list_.size()).c_str());
                auto& app = Application::GetInstance();
                Display* display = Board::GetInstance().GetDisplay();
                //循环加入到播放列表
                std::string msg = "找到本地歌曲:\n";
                app.ClearPlayList();
                int idx = 0;
                for (const auto& music : misuc_list_) {
                    PlayInfo info;
                    info.name = music;
                    info.type = PlayingType::LocalAudio;
                    //最后一个不加\n
                    if (idx == misuc_list_.size() - 1) {
                        msg += std::to_string(idx++) + ". " + info.name;
                    } else {
                        msg += std::to_string(idx++) + ". " + info.name + "\n";
                    }
                    //display->SetChatMessage("assistant", msg.c_str());
                    app.AddToPlayList(info);
                }
                display->SetChatMessage("assistant", msg.c_str());
                ESP_LOGI(TAG, "localMusicSearch: 搜索结果 %d", misuc_list_.size());
                return std::to_string(misuc_list_.size()) + "首加入到播放列表,第一首:" + misuc_list_[0];
        });

        
        methods_.AddMethod("startLocalPlay", "开始播放（如果本地播放列表就绪，调用开始播放）",  ParameterList({
            //播放模式
            Parameter("model", "播放模式：0 顺序播放 1 随机播放", kValueTypeNumber, false),
            Parameter("start", "播放第几首:默认从0开始", kValueTypeNumber, false)
        }), [this](const ParameterList& parameters) {
                int start = parameters["start"].number();
                int model = parameters["model"].number();
                if (model < 0 || model > 1) {
                    model = 0;
                }
                if (start < 0) {
                    start = 0;
                }
                auto& app = Application::GetInstance();
                std::vector<PlayInfo> play_list_ = app.GetPlayList();
                if (play_list_.empty()) {
                    return "播放列表为空!";
                } 
                if (start >= play_list_.size()) {
                    start = 0;
                }
                ESP_LOGI(TAG, "startLocalPlay: model = %d,start = %d, size = %d",model, start, play_list_.size());
                PlayMode mode_ = static_cast<PlayMode>(model);
                ESP_LOGI(TAG, "startLocalPlay: %s", play_list_[0].name.c_str());
                app.StartPlaying(mode_, PlayingType::LocalAudio, start);
                return "开始播放";
        });

    }

private:
};

}

DECLARE_THING(LocalMp3Player);
