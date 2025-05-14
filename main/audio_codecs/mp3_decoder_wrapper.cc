#include "mp3_decoder_wrapper.h"
#include <minimp3.h>
#include <minimp3_ex.h>
#include <cstring>
#include <esp_log.h>
#define TAG "Mp3StreamDecoderWrapper"

Mp3StreamDecoderWrapper::Mp3StreamDecoderWrapper(int ouput_sample_rate)
    : mp3_decoder_(nullptr), last_frame_bytes_(0), sample_rate_(0), channels_(0), frame_samples_(0)
{
    output_sample_rate_ = ouput_sample_rate;
    mp3_decoder_ = new mp3dec_t;
    mp3dec_init((mp3dec_t*)mp3_decoder_);
    //初始化降采样
    resampler_48_ = simple_resampler_create(48000, output_sample_rate_, 1);
    resampler_44_ = simple_resampler_create(44100, output_sample_rate_, 1);
}

Mp3StreamDecoderWrapper::~Mp3StreamDecoderWrapper() {
    if (mp3_decoder_) {
        delete (mp3dec_t*)mp3_decoder_;
        mp3_decoder_ = nullptr;
    }
    simple_resampler_destroy(resampler_48_);
    simple_resampler_destroy(resampler_44_);
}

bool Mp3StreamDecoderWrapper::DecodeFrame(const uint8_t* data, size_t size, std::vector<int16_t>& pcm_out) {
    if (!mp3_decoder_ || !data || size == 0) return false;
    mp3dec_frame_info_t info;
    std::vector<int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
    int samples = mp3dec_decode_frame((mp3dec_t*)mp3_decoder_, data, size, pcm.data(), &info);
    //ESP_LOGI(TAG,"MP3 frame length: %zu, samples: %d, frame_bytes: %d", size, samples, info.frame_bytes);
    if (samples > 0 && info.frame_bytes > 0) {
        last_frame_bytes_ = info.frame_bytes;
        sample_rate_ = info.hz;
        channels_ = info.channels;
        frame_samples_ = samples;

        std::vector<int16_t> mono_pcm;
        if (info.channels == 2) {
                // 立体声转单声道
                mono_pcm.resize(samples);
                for (int i = 0; i < samples; ++i) {
                    mono_pcm[i] = (pcm[i * 2] + pcm[i * 2 + 1]) / 2;
                }
            } else {
                mono_pcm.assign(pcm.begin(), pcm.begin() + samples);
            }

        //判断采样率
        SimpleResampler* resampler = nullptr;
        if (sample_rate_ == output_sample_rate_) {
            // 采样率一致，无需重采样
            pcm_out.insert(pcm_out.end(), mono_pcm.begin(), mono_pcm.end());
            return true;
        } else {
            if (sample_rate_ == 48000) {
                resampler = resampler_48_;
                ESP_LOGD(TAG,"MP3 decode frame, samples: %d, frame_bytes: %d, %d->%d", samples, info.frame_bytes,
                        sample_rate_, output_sample_rate_);
            } else if (sample_rate_ == 44100) {
                resampler = resampler_44_;
                ESP_LOGD(TAG,"MP3 decode frame, samples: %d, frame_bytes: %d, %d->%d", samples, info.frame_bytes,
                        sample_rate_, output_sample_rate_);
            } else {
                pcm_out.insert(pcm_out.end(), mono_pcm.begin(), mono_pcm.end());
                return true;
            }
        }
 
        size_t out_samples = mono_pcm.size() * output_sample_rate_ / sample_rate_;
        std::vector<int16_t> resampled(out_samples);
        size_t resampled_size = simple_resampler_process(
                resampler,
                mono_pcm.data(), mono_pcm.size(),
                resampled.data(), resampled.size());
        pcm_out.insert(pcm_out.end(), resampled.begin(), resampled.begin() + resampled_size);
        //ESP_LOGI(TAG,"MP3 decode success, samples: %d, frame_bytes: %d", samples, info.frame_bytes);
        return true;
    } else {
        last_frame_bytes_ = (info.frame_bytes > 0) ? info.frame_bytes : 0;
        ESP_LOGI(TAG,"MP3 decode failed, samples: %d, frame_bytes: %d", samples, info.frame_bytes);
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