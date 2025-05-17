#include "http_net_stream.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cstring>
#include <esp_log.h>

class EspHttpNetworkStream : public NetworkStream {
public:
    explicit EspHttpNetworkStream(esp_http_client_handle_t handle, int64_t content_length)
        : handle_(handle), content_length_(content_length) {}
    
    ~EspHttpNetworkStream() override {
        if (handle_) esp_http_client_cleanup(handle_);
    }

    int Read(uint8_t* buf, size_t len) override {
        if (!handle_) return -1;
        int ret = esp_http_client_read(handle_, (char*)buf, len);
        return ret;
    }
    int64_t GetContentLength() const override { return content_length_; }

    void Close() override {
        if (handle_) {
            esp_http_client_close(handle_);
            esp_http_client_cleanup(handle_);
            handle_ = nullptr;
        }
    }
private:
    esp_http_client_handle_t handle_;
    int64_t content_length_ = 0;
};

std::unique_ptr<NetworkStream> OpenNetworkStream(const std::string& url) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.buffer_size = 4096 * 8;  // 增加缓冲区大小
    config.is_async = false;    // 确保同步模式
    config.disable_auto_redirect = false;
    //判断如果是https协议则使用证书捆绑 
    if (strncmp(url.c_str(), "https://", 8) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.skip_cert_common_name_check = true; // 跳过证书公用名检查
    }
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE("EspHttpNetworkStream", "Failed to initialize HTTP client");
        return nullptr;
    }
    
    // 设置请求头
    esp_http_client_set_header(client, "Accept", "audio/mpeg");
    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0");
    esp_http_client_set_header(client, "Connection", "keep-alive");  // 保持连接
    
    // 执行请求
    esp_err_t err = esp_http_client_open(client,0);
    if (err != ESP_OK) {
        ESP_LOGE("EspHttpNetworkStream", "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return nullptr;
    }
    
    // --- 步骤 2: 获取响应头
    int64_t content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI("EspHttpNetworkStream", "HTTP fetch_headers: %lld", content_length);
    if (content_length <= 0) {
        bool is_chunk = esp_http_client_is_chunked_response(client);
        ESP_LOGI("SimpleHttpGet", "is_chunk: %d", is_chunk);
        if (!is_chunk) {
            ESP_LOGE("EspHttpNetworkStream", "Invalid content length: %lld", content_length);
            esp_http_client_cleanup(client);
            return nullptr;
        }
    }

    // --- 检查状态码
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE("EspHttpNetworkStream", "HTTP status: %d", status);
        esp_http_client_cleanup(client);
        return nullptr;
    }

    ESP_LOGI("EspHttpNetworkStream", "Stream opened successfully");
    return std::make_unique<EspHttpNetworkStream>(client, content_length);
}