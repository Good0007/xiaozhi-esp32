#pragma once
#include <WiFi.h>
#include <HTTPClient.h>

// 伪代码：实际解码部分需用合适的库
class NetAudioPlayer {
public:
    bool connectToHost(const char* url, audo *_audio) {
        if (!WiFi.isConnected()) return false;

        HTTPClient http;
        http.begin(url);
        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            http.end();
            return false;
        }

        WiFiClient* stream = http.getStreamPtr();
        uint8_t buffer[2048];
        while (http.connected()) {
            int len = stream->available();
            if (len > 0) {
                int readLen = stream->readBytes(buffer, min(len, (int)sizeof(buffer)));
                // 解码buffer为PCM数据，假设decodeToPCM()为解码函数
                int16_t pcm[1024];
                int pcmLen = decodeToPCM(buffer, readLen, pcm, sizeof(pcm)/sizeof(pcm[0]));
                if (pcmLen > 0) {
                    _audio.output(pcm, pcmLen); // 调用你的I2S输出方法
                }
            }
            delay(1);
        }
        http.end();
        return true;
    }

    // 从本地路径播放 mp3/wav
    bool playFromFile(const char* path) {
        File file = SPIFFS.open(path, "r");
        if (!file) return false;

        uint8_t buffer[2048];
        while (file.available()) {
            int len = file.readBytes(buffer, sizeof(buffer));
            // 解码buffer为PCM数据，假设decodeToPCM()为解码函数
            int16_t pcm[1024];
            int pcmLen = decodeToPCM(buffer, len, pcm, sizeof(pcm)/sizeof(pcm[0]));
            if (pcmLen > 0) {
                _audio.output(pcm, pcmLen); // 调用你的I2S输出方法
            }
        }
        file.close();
        return true;
    }

private:
    // 你需要实现或集成实际的解码函数
    int decodeToPCM(const uint8_t* in, int inLen, int16_t* out, int outLen) {
        // 这里需要调用实际的MP3/AAC等解码库
        // 返回解码后的PCM样本数
        return 0;
    }
};