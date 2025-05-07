#include "mp3_decoder_wrapper.h"
#include <minimp3.h>
#include <minimp3_ex.h>
#include <cstring>
#include <esp_log.h>
#define TAG "Mp3StreamDecoderWrapper"

Mp3StreamDecoderWrapper::Mp3StreamDecoderWrapper()
    : mp3_decoder_(nullptr), last_frame_bytes_(0), sample_rate_(0), channels_(0), frame_samples_(0)
{
    mp3_decoder_ = new mp3dec_t;
    mp3dec_init((mp3dec_t*)mp3_decoder_);
}

Mp3StreamDecoderWrapper::~Mp3StreamDecoderWrapper() {
    if (mp3_decoder_) {
        delete (mp3dec_t*)mp3_decoder_;
        mp3_decoder_ = nullptr;
    }
}

int find_sync(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size - 1; ++i) {
        if ((data[i] == 0xFF) && ((data[i + 1] & 0xE0) == 0xE0)) {
            return i;
        }
    }
    return -1;
}

bool Mp3StreamDecoderWrapper::DecodeFrame(const uint8_t* data, size_t size, std::vector<int16_t>& pcm_out) {
    if (!mp3_decoder_ || !data || size == 0) return false;

    mp3dec_frame_info_t info;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int sync_pos = find_sync(data, size);
    ESP_LOGI(TAG, "MP3 frame length: %zu, sync_pos: %d", size, sync_pos);
    int samples = mp3dec_decode_frame((mp3dec_t*)mp3_decoder_, data, size, pcm, &info);
    ESP_LOGI(TAG,"MP3 frame length: %zu, samples: %d, frame_bytes: %d", size, samples, info.frame_bytes);
    if (samples > 0 && info.frame_bytes > 0) {
        last_frame_bytes_ = info.frame_bytes;
        sample_rate_ = info.hz;
        channels_ = info.channels;
        frame_samples_ = samples;
        pcm_out.insert(pcm_out.end(), pcm, pcm + samples); // 修正
        return true;
    } else {
        last_frame_bytes_ = (info.frame_bytes > 0) ? info.frame_bytes : 0;
        return false;
    }
}

size_t Mp3StreamDecoderWrapper::LastFrameBytes() const {
    return last_frame_bytes_;
}

size_t Mp3StreamDecoderWrapper::FrameSamples() const {
    return frame_samples_;
}

int Mp3StreamDecoderWrapper::SampleRate() const {
    return sample_rate_;
}

int Mp3StreamDecoderWrapper::Channels() const {
    return channels_;
}