#include "simple_resampler.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SimpleResampler *simple_resampler_create(spx_uint32_t in_rate, spx_uint32_t out_rate, spx_uint32_t channels) {
    SimpleResampler *st = (SimpleResampler *)calloc(1, sizeof(SimpleResampler));
    if (!st) return NULL;
    st->in_rate = in_rate;
    st->out_rate = out_rate;
    st->channels = channels;
    st->ratio = (float)in_rate / (float)out_rate;
    st->buffer_len = 1024 * channels;
    st->buffer = (float *)calloc(st->buffer_len, sizeof(float));
    st->buffer_pos = 0;
    return st;
}

void simple_resampler_destroy(SimpleResampler *st) {
    if (st) {
        free(st->buffer);
        free(st);
    }
}

size_t simple_resampler_process(SimpleResampler *st,
                               const spx_int16_t *in, size_t in_len,
                               spx_int16_t *out, size_t out_len) {
    if (!st || !in || !out) return 0;
    size_t in_frames = in_len / st->channels;
    size_t out_frames = out_len / st->channels;
    float pos = 0.0f;
    size_t out_idx = 0;

    for (size_t i = 0; i < out_frames; ++i) {
        size_t idx = (size_t)pos;
        float frac = pos - idx;
        for (size_t ch = 0; ch < st->channels; ++ch) {
            int16_t s0 = (idx < in_frames) ? in[idx * st->channels + ch] : 0;
            int16_t s1 = ((idx + 1) < in_frames) ? in[(idx + 1) * st->channels + ch] : s0;
            float sample = (1.0f - frac) * s0 + frac * s1;
            out[out_idx * st->channels + ch] = (int16_t)sample;
        }
        pos += st->ratio;
        out_idx++;
    }
    return out_idx * st->channels;
}
