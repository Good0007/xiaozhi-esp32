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

class MusicSearch {
public:
    // 搜索音乐，返回结果列表
    static std::vector<MusicInfo> Search(
        const std::string& keyword, 
        int count = 10, int page = 1, 
        const std::string& source = "netease_album");

    // 搜索音乐，获取一个播放列表
    static std::vector<PlayInfo> GetPlayList(
        const std::string& keyword, 
        const int limit = 5,
        const std::string& source = "netease");

    // 随机获取播放信息
    static PlayInfo getRandomPlayInfo(const std::string& keyword, const std::string& source = "netease");

    // 获取播放地址
    static std::string GetPlayUrl(int32_t id, const std::string& source = "netease");

    //根据关键字搜索歌曲，返回PlayInfo对象
    static PlayInfo getPlayInfo(const std::string& keyword, const std::string& source = "netease");
};