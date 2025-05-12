#pragma once

#include <vector>
#include <memory>
#include "dsps_resampling.h"
#include "esp_log.h"

class DspResampler {
public:
    DspResampler();
    ~DspResampler();
    
    /**
     * @brief 配置重采样器，支持多通道
     * 
     * @param src_rate 源采样率(Hz)
     * @param dst_rate 目标采样率(Hz)
     * @param src_channels 源通道数(1=单声道，2=立体声)
     * @param dst_channels 目标通道数(1=单声道，2=立体声)
     * @return true 配置成功
     * @return false 配置失败
     */
    bool Configure(int src_rate, int dst_rate, int src_channels = 1, int dst_channels = 1);
    
    // ...现有方法...
    
    /**
     * @brief 获取源通道数
     * @return int 源通道数(1=单声道，2=立体声)
     */
    int GetSourceChannels() const { return src_channels_; }
    
    /**
     * @brief 获取目标通道数
     * @return int 目标通道数(1=单声道，2=立体声)
     */
    int GetDestChannels() const { return dst_channels_; }
    
private:
    int src_rate_ = 0;               // 源采样率
    int dst_rate_ = 0;               // 目标采样率
    int src_channels_ = 1;           // 源通道数
    int dst_channels_ = 1;           // 目标通道数
    float ratio_ = 1.0f;             // 重采样比例
    std::unique_ptr<float[]> coeffs_; // 滤波器系数
    std::unique_ptr<float[]> delay_; // 延迟缓冲区
    dsps_resampling_cfg_t config_;   // 重采样配置
    bool initialized_ = false;       // 初始化标志
    
    // 处理立体声到单声道的转换
    std::vector<int16_t> StereoToMono(const std::vector<int16_t>& stereo_data);
    
    static constexpr int MAX_COEFFS_NUM = 64; // 最大系数数量
};