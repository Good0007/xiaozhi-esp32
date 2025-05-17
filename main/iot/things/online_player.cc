#include "iot/thing.h"
#include "board.h"
#include "display/display.h"
#include <esp_log.h>
#include "audio_codec.h"
#include "application.h"
#include "online_music.h"
#define TAG "OnlineMp3Player"

namespace iot {

// 这里仅定义 AadioPlayer 的属性和方法，不包含具体的实现
class OnlineMp3Player : public Thing {
public:
    OnlineMp3Player() : Thing("OnlineMp3Player", "音乐播放器：支持搜索音乐、指定音乐名称播放、指定歌手|专辑|关键字播放（当用户需要听歌优先从这里搜索！）") {
        properties_.AddStringProperty("musicState", "播放器状态：0 空闲 1 播放中", [this]() -> std::string {
            auto& app = Application::GetInstance();
            std::string state_ = (app.GetPlayingType() == PlayingType::Mp3Stream ? "1" : "0");
            ESP_LOGI(TAG, "musicState: %s", state_.c_str());
            return state_;
        });

        properties_.AddStringProperty("playListInfo", "获取播放列表信息", [this]() -> std::string {
             std::vector<PlayInfo> play_list_ = Application::GetInstance().GetPlayList();
             std::string first_name = "";
            if (!play_list_.empty()) {
                first_name = play_list_[0].name;
            }
             ESP_LOGI(TAG, "playListInfo: count = %d, first_name = %s",play_list_.size(), first_name.c_str());
             return std::to_string(play_list_.size())+"首加入到播放列表,第一首:" + first_name;
        });

        methods_.AddMethod("musicSearch", "搜索音乐",  ParameterList({
            Parameter("keyword", "搜索关键字：可以是音乐名称/歌手等", kValueTypeString, true),
            Parameter("count", "搜索数量：默认10 (如果用户明确搜索指定歌曲，就传入1)", kValueTypeNumber, false),
        }), [this](const ParameterList& parameters) -> std::string {
                std::string keyword = parameters["keyword"].string();
                int count = parameters["count"].number();
                if (count <= 0) {
                    count = 10;
                }
                ESP_LOGI(TAG, "musicSearch: %s,count = %d", keyword.c_str(), count);
                std::vector<MusicInfo> misuc_list_ = MusicSearch::Search(keyword, count);
                //循环搜索结果，拼接成一个List结果
                ESP_LOGI(TAG, "musicSearch: 找到 %s 首音乐！", std::to_string(misuc_list_.size()).c_str());
                auto& app = Application::GetInstance();
                Display* display = Board::GetInstance().GetDisplay();
                //循环加入到播放列表
                std::string msg = "找到以下歌曲:\n";
                app.ClearPlayList();
                int idx = 0;
                for (const auto& music : misuc_list_) {
                    PlayInfo info;
                    info.id = music.id;
                    info.name = music.name;
                    msg += std::to_string(idx++) + ". " + info.name + "\n";
                    //display->SetChatMessage("assistant", msg.c_str());
                    app.AddToPlayList(info);
                }
                display->SetChatMessage("assistant", msg.c_str());
                ESP_LOGI(TAG, "musicSearch: 搜索结果 %d", misuc_list_.size());
                return std::to_string(misuc_list_.size()) + "首加入到播放列表,第一首:" + misuc_list_[0].name;
        });

        
        methods_.AddMethod("startPlay", "开始播放（如果播放列表就绪，调用开始播放）",  ParameterList({
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
                ESP_LOGI(TAG, "startPlay: model = %d,start = %d, size = %d",model, start, play_list_.size());
                PlayMode mode_ = static_cast<PlayMode>(model);
                ESP_LOGI(TAG, "startPlay: %s", play_list_[0].name.c_str());
                app.StartPlaying(mode_,start);
                return "开始播放";
        });

        /**
        methods_.AddMethod("musicSearchPlay", "指定歌手、专辑播放（指定歌手、专辑等）",  ParameterList({
            Parameter("keyword", "歌手、专辑描述等关键字", kValueTypeString, true)
        }), [this](const ParameterList& parameters) {
                std::string keyword = parameters["keyword"].string();
                ESP_LOGI(TAG, "musicSearchPlay-关键字: %s", keyword.c_str());
                std::vector<MusicInfo> play_list_ = MusicSearch::Search(keyword);
                if (play_list_.empty()) {
                    return "没有找到结果";
                }
                ESP_LOGI(TAG, "musicSearchPlay-搜索结果: 找到 %s 首音乐！", std::to_string(play_list_.size()).c_str());
                std::vector<PlayInfo> play_info_list;
                for (const auto& music : play_list_) {
                    PlayInfo info;
                    info.id = music.id;
                    info.name = music.name;
                    play_info_list.push_back(info);
                }
                auto& app = Application::GetInstance();
                app.ChangePlaying(PlayingType::Mp3Stream, play_info_list);
                return "播放成功";
        });
       

        // 播放指定的音乐
        methods_.AddMethod("musicPlay", "指定歌曲名称播放（不要传入其他关键词）",  ParameterList({
            Parameter("keyword", "歌曲名称（传入明确的歌曲名称，如无明确歌曲名称，使用musicSearchPlay播放）", kValueTypeString, true)
        }), [this](const ParameterList& parameters) -> std::string {
                std::string keyword = parameters["keyword"].string();
                ESP_LOGI(TAG, "musicPlay: %s", keyword.c_str());
                PlayInfo play_info = MusicSearch::getPlayInfo(keyword);
                if (!play_info.url.empty()) {
                    auto& app = Application::GetInstance();
                    ESP_LOGI(TAG, "musicPlay-搜索歌曲: name = %s, url = %s",play_info.name.c_str(), play_info.url.c_str());
                     std::vector<PlayInfo> play_list{play_info};
                    app.ChangePlaying(PlayingType::Mp3Stream, play_list);
                    return "播放成功";
                } else {
                    ESP_LOGI(TAG, "No results found for keyword: %s", keyword.c_str());
                }
                return "没有找到结果";
        });
         */

    }

private:
};

} // namespace iot

DECLARE_THING(OnlineMp3Player);
