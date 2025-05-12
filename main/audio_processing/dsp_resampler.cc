
#include "dsp_resampler.h"
#include <cstring>
#include "esp_log.h"
#include "dsps_math.h"

#define TAG "DspResampler"

DspResampler::DspResampler() {
    // 分配滤波器系数内存
    coeffs_ = std::unique_ptr<float[]>(new float[MAX_COEFFS_NUM]);
    delay_ = std::unique_ptr<float[]>(new float[MAX_COEFFS_NUM]);
    
    // 初始化DSP库
    dsps_resampling_init(coeffs_.get(), MAX_COEFFS_NUM);
    
    // 清零内存
    memset(coeffs_.get(), 0, MAX_COEFFS_NUM * sizeof(float));
    memset(delay_.get(), 0, MAX_COEFFS_NUM * sizeof(float));
}

DspResampler::~DspResampler() {
    // 析构时智能指针会自动释放内存
}
bool DspResampler::Configure(int src_rate, int dst_rate, int src_channels, int dst_channels) {
    if (src_rate <= 0 || dst_rate <= 0) {
        ESP_LOGE(TAG, "Invalid sample rates: src=%d, dst=%d", src_rate, dst_rate);
        return false;
    }
    
    if (src_channels != 1 && src_channels != 2) {
        ESP_LOGE(TAG, "Invalid source channels: %d (must be 1 or 2)", src_channels);
        return false;
    }
    
    if (dst_channels != 1 && dst_channels != 2) {
        ESP_LOGE(TAG, "Invalid destination channels: %d (must be 1 or 2)", dst_channels);
        return false;
    }
    
    src_rate_ = src_rate;
    dst_rate_ = dst_rate;
    src_channels_ = src_channels;
    dst_channels_ = dst_channels;
    ratio_ = static_cast<float>(dst_rate) / src_rate;
    
    // 清零延迟缓冲区
    memset(delay_.get(), 0, MAX_COEFFS_NUM * sizeof(float));
    
    // 创建并配置重采样器
    memset(&config_, 0, sizeof(config_));
    config_.src_rate = src_rate_;
    config_.dst_rate = dst_rate_;
    config_.src_ch = src_channels_;
    config_.dst_ch = dst_channels_;
    config_.sample_bits = 16;
    config_.coeff = coeffs_.get();
    config_.coeff_num = MAX_COEFFS_NUM;
    config_.delay = delay_.get();
    
    initialized_ = true;
    ESP_LOGI(TAG, "Resampler configured: %d Hz -> %d Hz, channels: %d -> %d, ratio=%.3f", 
             src_rate_, dst_rate_, src_channels_, dst_channels_, ratio_);
    return true;
}

std::vector<int16_t> DspResampler::Process(const std::vector<int16_t>& input) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Resampler not initialized");
        return input;
    }
    
    // 特殊处理：空输入
    if (input.empty()) {
        return {};
    }
    
    // 处理立体声到单声道的转换
    std::vector<int16_t> processed_input = input;
    if (src_channels_ == 2 && dst_channels_ == 1) {
        processed_input = StereoToMono(input);
    }
    
    // 如果采样率相同，只需转换通道
    if (src_rate_ == dst_rate_) {
        return processed_input;
    }
    
    // 计算输出尺寸
    int output_size = GetOutputSize(processed_input.size());
    std::vector<int16_t> output(output_size);
    
    // 执行重采样
    int ret = dsps_resampling_process_int16(&config_, 
                                           processed_input.data(), 
                                           output.data(), 
                                           processed_input.size() / src_channels_, 
                                           output.size() / dst_channels_);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Resampling failed, error code=%d", ret);
        return processed_input;  // 失败时返回处理过通道的数据
    }
    
    return output;
}

// 立体声转单声道实现
std::vector<int16_t> DspResampler::StereoToMono(const std::vector<int16_t>& stereo_data) {
    if (stereo_data.size() < 2) return stereo_data;
    
    int mono_size = stereo_data.size() / 2;
    std::vector<int16_t> mono_data(mono_size);
    
    for (int i = 0; i < mono_size; i++) {
        // 混合左右声道 (L+R)/2
        int32_t mixed = (static_cast<int32_t>(stereo_data[i*2]) + 
                         static_cast<int32_t>(stereo_data[i*2+1])) / 2;
        mono_data[i] = static_cast<int16_t>(mixed);
    }
    
    return mono_data;
}

int DspResampler::GetOutputSize(int input_size) const {
    if (!initialized_) return input_size;
    
    // 考虑通道数变化
    int sample_count = input_size / src_channels_;
    
    // 采样率转换
    int output_sample_count = (src_rate_ == dst_rate_) ? 
        sample_count : static_cast<int>(sample_count * ratio_);
        
    // 返回考虑输出通道数的总样本数
    return output_sample_count * dst_channels_;
}