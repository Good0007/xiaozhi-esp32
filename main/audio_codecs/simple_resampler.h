#pragma once

#include <stdint.h>
#include <stddef.h>

typedef int16_t spx_int16_t;
typedef uint32_t spx_uint32_t;

typedef struct {
    spx_uint32_t in_rate;
    spx_uint32_t out_rate;
    spx_uint32_t channels;
    float ratio;
    float *buffer;
    size_t buffer_len;
    size_t buffer_pos;
} SimpleResampler;

// 创建重采样器
SimpleResampler *simple_resampler_create(spx_uint32_t in_rate, spx_uint32_t out_rate, spx_uint32_t channels);

// 销毁重采样器
void simple_resampler_destroy(SimpleResampler *st);

// 简单线性插值重采样（支持多声道）
size_t simple_resampler_process(SimpleResampler *st,
                               const spx_int16_t *in, size_t in_len,
                               spx_int16_t *out, size_t out_len);