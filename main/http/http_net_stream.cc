#include "http_net_stream.h"
#include <esp_http_client.h>
#include <cstring>

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
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return nullptr;
    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return nullptr;
    }
    return std::make_unique<EspHttpNetworkStream>(client);
}