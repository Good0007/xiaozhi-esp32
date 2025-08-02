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

#define AUTH_TOKEN "d5xohNTsUsoGb65x"


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

// 简单 POST 请求，返回响应内容（失败返回空字符串）
std::string SimpleHttpPost(const std::string& url, 
    const std::string& post_data, 
    const std::string& content_type = "application/x-www-form-urlencoded; charset=UTF-8",
    const std::string& auth_token = "") {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 10000; // 10秒超时
    config.crt_bundle_attach = esp_crt_bundle_attach; // 使用证书捆绑
    config.buffer_size = 2048; // 设置缓冲区大小
    config.skip_cert_common_name_check = true; // 跳过证书公用名检查
    config.is_async = false;    // 确保同步模式
    config.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE("SimpleHttpPost", "Failed to init http client");
        return "";
    }

    // 设置请求头
    esp_http_client_set_header(client, "Content-Type", content_type.c_str());
    if (!auth_token.empty()) {
        esp_http_client_set_header(client, "auth-token", auth_token.c_str());
    }
    esp_http_client_set_post_field(client, post_data.c_str(), post_data.length());

    esp_err_t err = esp_http_client_open(client, post_data.length());
    if (err != ESP_OK) {
        ESP_LOGE("SimpleHttpPost", "Failed to open http connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return "";
    }

    // 写入POST数据
    int write_len = esp_http_client_write(client, post_data.c_str(), post_data.length());
    if (write_len < 0) {
        ESP_LOGE("SimpleHttpPost", "Failed to write POST data");
        esp_http_client_cleanup(client);
        return "";
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI("SimpleHttpPost", "fetch_headers: %lld", content_length);
    if (content_length <= 0) {
        bool is_chunk = esp_http_client_is_chunked_response(client);
        ESP_LOGI("SimpleHttpPost", "is_chunk: %d", is_chunk);
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

/***
 * 搜索音乐
 * 根据 curl 注释更新的新接口：
 * curl -X POST https://chenweikang.top/v1/music -H "auth-token:d5xohNTsUsoGb65x" \
 *   -d "types=search" \
 *   -d "count=2" \
 *   -d "source=netease" \
 *   -d "pages=1" \
 *   -d "name=%E5%91%A8%E6%9D%B0%E4%BC%A6"
 * 响应格式：{"code":0,"msg":"success","data":[{"id":210049,"name":"布拉格广场","artist":["蔡依林","周杰伦"],"album":"看我72变","pic_id":"109951171530950893","url_id":210049,"lyric_id":210049,"source":"netease"}]}
 */
std::vector<MusicInfo> MusicSearch::Search(const std::string& keyword, int count, int page, const std::string& source) {
    std::string url = MUSIC_API;
    std::string post_data = "types=search&count=" + std::to_string(count) +
                            "&source=" + source +
                            "&pages=" + std::to_string(page) +
                            "&name=" + UrlEncode(keyword);

    ESP_LOGI("MusicSearch", "POST data: %s", post_data.c_str());
    std::string body = SimpleHttpPost(url, post_data, "application/x-www-form-urlencoded; charset=UTF-8", AUTH_TOKEN);
    ESP_LOGI("MusicSearch", "Body: %s", body.c_str());
    
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return {};
    }

    // 检查响应码
    cJSON* code = cJSON_GetObjectItem(root, "code");
    if (!code || !cJSON_IsNumber(code) || code->valueint != 0) {
        ESP_LOGE("MusicSearch", "API returned error code: %d", code ? code->valueint : -1);
        cJSON_Delete(root);
        return {};
    }

    // 获取数据数组
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (!data || !cJSON_IsArray(data)) {
        ESP_LOGE("MusicSearch", "Invalid data format");
        cJSON_Delete(root);
        return {};
    }

    std::vector<MusicInfo> result;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, data) {
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
 * 获取播放地址
 * 根据 curl 注释更新的新接口：
 * curl -X POST https://chenweikang.top/v1/music -H "auth-token:d5xohNTsUsoGb65x" \
 *   -d "types=url&id=566900022&source=netease"
 * 响应格式：{"code":0,"msg":"success","data":{"url":"https://music.163.com/song/media/outer/url?id=566900022.mp3"}}
 */
std::string MusicSearch::GetPlayUrl(const int32_t id, const std::string& source) {
    std::string url = MUSIC_API;
    std::string post_data = "types=url&id=" + std::to_string(id) +
                            "&source=" + source +
                            "&br=" + PLAY_BR;
                            
    ESP_LOGI("GetPlayUrl", "POST data: %s", post_data.c_str());
    std::string body = SimpleHttpPost(url, post_data, "application/x-www-form-urlencoded; charset=UTF-8", AUTH_TOKEN);
    ESP_LOGI("GetPlayUrl", "Body: %s", body.c_str());
    
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return {};
    }

    // 检查响应码
    cJSON* code = cJSON_GetObjectItem(root, "code");
    if (!code || !cJSON_IsNumber(code) || code->valueint != 0) {
        ESP_LOGE("GetPlayUrl", "API returned error code: %d", code ? code->valueint : -1);
        cJSON_Delete(root);
        return {};
    }

    // 获取数据对象
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (!data || !cJSON_IsObject(data)) {
        ESP_LOGE("GetPlayUrl", "Invalid data format");
        cJSON_Delete(root);
        return {};
    }

    std::string result;
    cJSON* url_item = cJSON_GetObjectItem(data, "url");
    if (url_item && cJSON_IsString(url_item)) {
        result = url_item->valuestring;
        ESP_LOGI("GetPlayUrl", "Play URL: %s", result.c_str());
    }
    cJSON_Delete(root);
    return result;
}