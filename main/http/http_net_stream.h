#pragma once
#include <string>
#include <memory>

class NetworkStream {
public:
    virtual ~NetworkStream() = default;
    virtual int Read(uint8_t* buf, size_t len) = 0;
    virtual void Close() = 0;
    virtual int64_t GetContentLength() const { return 0; }
};


std::unique_ptr<NetworkStream> OpenNetworkStream(const std::string& url);