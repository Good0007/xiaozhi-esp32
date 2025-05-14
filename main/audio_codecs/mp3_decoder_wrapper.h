#pragma once
#include <vector>
#include <cstdint>
#include "simple_resampler.h"

class Mp3StreamDecoderWrapper {
public:
    Mp3StreamDecoderWrapper(int ouput_sample_rate = 24000);
    ~Mp3StreamDecoderWrapper();

    // 输入一段MP3数据，输出PCM数据，返回是否解出一帧
    // consumed_bytes: 本次消耗的MP3字节数
    // 返回true表示解码出一帧PCM，false表示需要更多数据
    bool DecodeFrame(const uint8_t* data, size_t size, std::vector<int16_t>& pcm_out);

    // 返回上一帧消耗的MP3字节数
    size_t LastFrameBytes() const;

    // 返回每帧PCM采样数（如1152）
    size_t FrameSamples() const;

    // 返回采样率
    int SampleRate() const;

    // 返回声道数
    int Channels() const;

private:
    // 内部解码器状态
    void* mp3_decoder_; // 具体类型根据库而定
    size_t last_frame_bytes_;
    int sample_rate_;
    int channels_;
    size_t frame_samples_;
    int output_sample_rate_; // 目标采样率
    SimpleResampler* resampler_48_; //48kHz转24kHz
    SimpleResampler* resampler_44_; //44.1kHz转24kHz
};