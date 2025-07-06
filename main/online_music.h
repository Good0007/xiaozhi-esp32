#pragma once
#include <string>
#include <vector>
#include <map>
#include "application.h"

struct MusicInfo {
    int id;
    std::string name;
    std::string album;
    std::string lyric_id;
    std::vector<std::string> artist;
    std::string source;
};
// 定义surce类型
enum class SourceType {
    netease,       // 网易云音乐
    netease_album, // 网易云音乐专辑
    kugou,         // 酷狗音乐
    kuwo,          // 酷我音乐
};

//默认源
#define DEFAULT_SOURCE "netease"
#define PLAY_VERSION "2025.6.16"
#define PLAY_BR "192"

class MusicSearch {
public:
    // 搜索音乐，返回结果列表
    static std::vector<MusicInfo> Search(
        const std::string& keyword, 
        int count = 10, int page = 1, 
        //netease_album 专辑， netease 歌手
        const std::string& source = DEFAULT_SOURCE);

    // 搜索音乐，获取一个播放列表
    static std::vector<PlayInfo> GetPlayList(
        const std::string& keyword, 
        const int limit = 5,
        const std::string& source = DEFAULT_SOURCE);

    // 随机获取播放信息
    static PlayInfo getRandomPlayInfo(const std::string& keyword, const std::string& source = DEFAULT_SOURCE);

    // 获取播放地址
    static std::string GetPlayUrl(int32_t id, const std::string& source = DEFAULT_SOURCE);

    //根据关键字搜索歌曲，返回PlayInfo对象
    static PlayInfo getPlayInfo(const std::string& keyword, const std::string& source = DEFAULT_SOURCE);
};