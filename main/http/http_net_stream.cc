#include "http_net_stream.h"
#include <esp_http_client.h>
#include <cstring>
#include <esp_log.h>

class EspHttpNetworkStream : public NetworkStream {
public:
    explicit EspHttpNetworkStream(esp_http_client_handle_t handle) : handle_(handle) {}
    ~EspHttpNetworkStream() override {
        if (handle_) esp_http_client_cleanup(handle_);
    }
    int Read(uint8_t* buf, size_t len) override {
        if (!handle_) return -1;
        int ret = esp_http_client_read(handle_, (char*)buf, len);
        return ret;
    }
    void Close() override {
        if (handle_) {
            esp_http_client_close(handle_);
            esp_http_client_cleanup(handle_);
            handle_ = nullptr;
        }
    }
private:
    esp_http_client_handle_t handle_;
};

std::unique_ptr<NetworkStream> OpenNetworkStream(const std::string& url) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.buffer_size = 4096 * 2;  // 增加缓冲区大小
    config.buffer_size_tx = 512;
    config.is_async = false;    // 确保同步模式
    config.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE("HTTP", "Failed to initialize HTTP client");
        return nullptr;
    }
    
    // 设置请求头
    esp_http_client_set_header(client, "Accept", "audio/mpeg");
    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0");
    esp_http_client_set_header(client, "Connection", "keep-alive");  // 保持连接
    
    // 执行请求
    esp_err_t err = esp_http_client_open(client,0);
    if (err != ESP_OK) {
        ESP_LOGE("HTTP", "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return nullptr;
    }
    
    // --- 步骤 2: 获取响应头
    if (esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGE("HTTP", "Failed to fetch headers");
        esp_http_client_cleanup(client);
        return nullptr;
    }

    // --- 检查状态码
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE("HTTP", "HTTP status: %d", status);
        esp_http_client_cleanup(client);
        return nullptr;
    }

    ESP_LOGI("HTTP", "Stream opened successfully");
    return std::make_unique<EspHttpNetworkStream>(client);
    
    return std::make_unique<EspHttpNetworkStream>(client);
}