#include "online_music.h"
#include "board.h"
#include <cJSON.h>
#include <regex>
#include <esp_log.h>
#include <string>
#include <vector>
#include <sys/time.h>
#include <iomanip>
#include <sstream>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include "mbedtls/md5.h"
#include <sstream>

//_后边是时间戳 1746943308218
#define MUSIC_SEARCH_URL "https://music.gdstudio.xyz/api.php?callback=jQuery111307358914372812725_"
#define MUSIC_GET_PLAY_URL "https://music.gdstudio.xyz/api.php?callback=jQuery111305091133827048107_"

/**
 * 
 * mkPlayer = {
    "api": "api.php",
    "loadcount": 20,
    "maxerror": 3,
    "method": "POST",
    "defaultlist": 1,
    "fadeInOut": 3,
    "autoplay": false,
    "coverbg": true,
    "mcoverbg": false,
    "dotshine": false,
    "showtime": true,
    "mdotshine": false,
    "lyrictitle": false,
    "homelist": false,
    "refreshlist": false,
    "autoeq": false,
    "desktoplyrics": false,
    "nameformat": 0,
    "volume": 1,
    "version": "2025.4.27",
    "email": "gdstudio@email.com",
    "apiurl": "https://music-api.gdstudio.xyz/api.php",
    "appdir": "gdmusic.apk",
    "appver": 1.1,
    "dldir": "DesktopLyrics.exe",
    "dlver": 1,
    "proxyUrl": "https://cors-proxy.gdstudio.workers.dev"
}
 * 
 */
// 计算字符串的MD5，返回32位小写hex字符串
std::string md5(const std::string& input) {
    unsigned char digest[16];
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);
    mbedtls_md5_update(&ctx, reinterpret_cast<const unsigned char*>(input.data()), input.size());
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);
    std::ostringstream oss;
    for (int i = 0; i < 16; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    return oss.str();
}

// 生成s参数
std::string calc_s_param(const std::string& id, const std::string& version) {
    // 1. 补零拼接
    std::stringstream v1ss;
    std::istringstream iss(version);
    std::string seg;
    while (std::getline(iss, seg, '.')) {
        if (seg.length() == 1) v1ss << "0" << seg;
        else v1ss << seg;
    }
    std::string v1 = v1ss.str();
    // 2. 反转
    std::string v2 = v1;
    std::reverse(v2.begin(), v2.end());
    // 3. 拼接
    std::string str = v1 + "s" + id + "s" + v2;
    // 4. md5
    std::string md5Result = md5(str);
    // 5. 取最后8位并转大写
    std::string s = md5Result.substr(md5Result.length() - 8);
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// 简单 GET 请求，返回响应内容（失败返回空字符串）
std::string SimpleHttpGet(const std::string& url) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000; // 10秒超时
    config.crt_bundle_attach = esp_crt_bundle_attach; // 使用证书捆绑
    config.buffer_size = 2048; // 设置缓冲区大小
    config.skip_cert_common_name_check = true; // 跳过证书公用名检查]
    config.is_async = false;    // 确保同步模式
    config.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE("SimpleHttpGet", "Failed to init http client");
        return "";
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE("SimpleHttpGet", "Failed to open http connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return {};
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI("SimpleHttpGet", "fetch_headers: %lld", content_length);
    if (content_length <= 0) {
        bool is_chunk = esp_http_client_is_chunked_response(client);
        ESP_LOGI("SimpleHttpGet", "is_chunk: %d", is_chunk);
    }
    
    std::string result;
    char buffer[512];
    int read_len = 0;
    while ((read_len = esp_http_client_read(client, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[read_len] = 0;
        result += buffer;
    }

    esp_http_client_cleanup(client);
    return result;
}

std::string UrlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (auto c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << int((unsigned char)c);
        }
    }
    return escaped.str();
}

std::vector<PlayInfo> MusicSearch::GetPlayList(const std::string& keyword,int limit, const std::string& source) {
    std::vector<MusicInfo> results = MusicSearch::Search(keyword, limit, 1, source);
    if (results.empty()) {
        ESP_LOGI("getPlayList", "No results found for keyword: %s", keyword.c_str());
        return {};
    }
    std::vector<PlayInfo> play_list;
    for (const auto& info : results) {
        PlayInfo play_info;
        play_info.name = info.name;
        play_info.tag = 0; // 音乐
        play_info.url = MusicSearch::GetPlayUrl(info.id, info.source);
        play_info.type = PlayingType::Mp3Stream;
        ESP_LOGI("getPlayList", "ID: %d, Name: %s, URL: %s", info.id, play_info.name.c_str(), play_info.url.c_str());
        if (!play_info.url.empty()) {
            play_list.push_back(play_info);
        }
    }
    return play_list;
}

PlayInfo MusicSearch::getRandomPlayInfo(const std::string& keyword, const std::string& source) {
    std::vector<MusicInfo> results = MusicSearch::Search(keyword, 20, 1);
    if (results.empty()) {
        ESP_LOGI("getRandomPlayInfo", "No results found for keyword: %s", keyword.c_str());
        return PlayInfo{};
    }

    int max_attempts = 3;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // 随机选一个
        int idx = rand() % results.size();
        const MusicInfo& info = results[idx];
        PlayInfo play_info;
        play_info.id = info.id;
        play_info.name = info.name;
        play_info.tag = 0; // 音乐
        play_info.url = MusicSearch::GetPlayUrl(info.id, info.source);
        ESP_LOGI("getRandomPlayInfo", "Try %d: ID: %d, Name: %s, URL: %s", attempt + 1, info.id, play_info.name.c_str(), play_info.url.c_str());
        if (!play_info.url.empty()) {
            return play_info;
        }
        // 如果失败，移除该项，避免重复尝试
        results.erase(results.begin() + idx);
        if (results.empty()) break;
    }
    ESP_LOGI("getRandomPlayInfo", "Failed to get valid play url after %d attempts", max_attempts);
    return PlayInfo{};
}

PlayInfo MusicSearch::getPlayInfo(const std::string& keyword, const std::string& source) {
    PlayInfo play_info;
    std::vector<MusicInfo> results = MusicSearch::Search(keyword, 5, 1, source);
    if (!results.empty()) {
        play_info.name = results[0].name;
        play_info.url = MusicSearch::GetPlayUrl(results[0].id);
        play_info.tag = 0; // 音乐
        ESP_LOGI("getPlayInfo", "ID: %d, Name: %s, URL: %s", results[0].id, play_info.name.c_str(), play_info.url.c_str());
    } else {
        ESP_LOGI("getPlayInfo", "No results found for keyword: %s", keyword.c_str());
    }
    return play_info;
}

std::vector<MusicInfo> MusicSearch::Search(const std::string& keyword, int count, int page, const std::string& source) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t ms = static_cast<uint64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    std::string url = MUSIC_SEARCH_URL + std::to_string(ms) + "&types=search&count=" + std::to_string(count) +
                            "&source=" + source +
                            "&pages=" + std::to_string(page) +
                            "&name=" + UrlEncode(keyword);
    ESP_LOGI("MusicSearch", "HTTP GET: %s", url.c_str());
    std::string body = SimpleHttpGet(url);
    //delete http;
    ESP_LOGI("MusicSearch", "Body: %s", body.c_str());
    // 处理 JSONP 响应，提取括号内 JSON
    size_t l = body.find('('), r = body.rfind(')');
    if (l == std::string::npos || r == std::string::npos || r <= l) return {};
    std::string json = body.substr(l + 1, r - l - 1);
    ESP_LOGI("MusicSearch", "JSON: %s", json.c_str());
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return {};
    }
    std::vector<MusicInfo> result;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        MusicInfo info;
        cJSON* id = cJSON_GetObjectItem(item, "id");
        cJSON* name = cJSON_GetObjectItem(item, "name");
        cJSON* album = cJSON_GetObjectItem(item, "album");
        cJSON* source = cJSON_GetObjectItem(item, "source");
        cJSON* lyric_id = cJSON_GetObjectItem(item, "lyric_id");
        if (id && cJSON_IsNumber(id)) info.id = id->valueint;
        if (name && cJSON_IsString(name)) info.name = name->valuestring;
        if (album && cJSON_IsString(album)) info.album = album->valuestring;
        if (source && cJSON_IsString(source)) info.source = source->valuestring;
        if (lyric_id && cJSON_IsString(lyric_id)) info.lyric_id = lyric_id->valuestring;
        cJSON* artistArr = cJSON_GetObjectItem(item, "artist");
        if (artistArr && cJSON_IsArray(artistArr)) {
            cJSON* artist = nullptr;
            cJSON_ArrayForEach(artist, artistArr) {
                if (artist && cJSON_IsString(artist))
                    info.artist.push_back(artist->valuestring);
            }
        }
        result.push_back(info);
    }
    cJSON_Delete(root);
    return result;
}

/***
 * 
 * //获取播放地址：传入上一步获取的id
 * https://music.gdstudio.xyz/api.php?callback=jQuery111305091133827048107_1746943401683&types=url&id=2683819218&source=netease&br=320&s=A8C37883
 * 
 * jQuery111305091133827048107_1746943401683({
	"url": "https://m701.music.126.net/20250511225508/8bb7042a63bba217aa2d1bc682636b05/jdymusic/obj/wo3DlMOGwrbDjj7DisKw/59695601882/042b/1f8b/7a5c/47f920bdad87770c03331a577f8d6dd7.mp3",
	"size": 9836205,
	"br": 320
})
 */
std::string MusicSearch::GetPlayUrl(const int32_t id, const std::string& source) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t ms = static_cast<uint64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    std::string url = MUSIC_GET_PLAY_URL + std::to_string(ms) + "&types=url&id=" + std::to_string(id) +
                            "&source=" + source +
                            "&br=320&s=" + calc_s_param(std::to_string(id), "2025.4.27");
    ESP_LOGI("GetPlayUrl", "HTTP GET: %s", url.c_str());
    std::string body = SimpleHttpGet(url);
    ESP_LOGI("GetPlayUrl", "Body: %s", body.c_str());
    // 处理 JSONP 响应，提取括号内 JSON
    size_t l = body.find('('), r = body.rfind(')');
    if (l == std::string::npos || r == std::string::npos || r <= l) return {};
    std::string json = body.substr(l + 1, r - l - 1);
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return {};
    }
    std::string result;
    cJSON* item = cJSON_GetObjectItem(root, "url");
    if (item && cJSON_IsString(item)) {
        result = item->valuestring;
        ESP_LOGI("MusicSearch", "Play URL: %s", result.c_str());
    }
    cJSON_Delete(root);
    return result;
}