#include "../include/convolution_filter.h"

#include <assert.h>
#include <string.h>

#if defined(__ANDROID__) && defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace {
#if defined(__ANDROID__) && defined(__aarch64__)
inline float dotProductNeon(const float *a, const float *b, int count) {
    int i = 0;
    float32x4_t acc0 = vdupq_n_f32(0.0f);
    float32x4_t acc1 = vdupq_n_f32(0.0f);
    float32x4_t acc2 = vdupq_n_f32(0.0f);
    float32x4_t acc3 = vdupq_n_f32(0.0f);

    for (; i + 15 < count; i += 16) {
        acc0 = vfmaq_f32(acc0, vld1q_f32(a + i), vld1q_f32(b + i));
        acc1 = vfmaq_f32(acc1, vld1q_f32(a + i + 4), vld1q_f32(b + i + 4));
        acc2 = vfmaq_f32(acc2, vld1q_f32(a + i + 8), vld1q_f32(b + i + 8));
        acc3 = vfmaq_f32(acc3, vld1q_f32(a + i + 12), vld1q_f32(b + i + 12));
    }

    float32x4_t acc = vaddq_f32(vaddq_f32(acc0, acc1), vaddq_f32(acc2, acc3));
    float result = vaddvq_f32(acc);
    for (; i < count; ++i) result += a[i] * b[i];
    return result;
}
#endif
}

ConvolutionFilter::ConvolutionFilter() {
    m_shiftRegister = nullptr;
    m_impulseResponse = nullptr;

    m_shiftOffset = 0;
    m_sampleCount = 0;
}

ConvolutionFilter::~ConvolutionFilter() {
    assert(m_shiftRegister == nullptr);
    assert(m_impulseResponse == nullptr);
}

void ConvolutionFilter::initialize(int samples) {
    m_sampleCount = samples;
    m_shiftOffset = 0;
    m_shiftRegister = new float[samples];
    m_impulseResponse = new float[samples];

    memset(m_shiftRegister, 0, sizeof(float) * samples);
    memset(m_impulseResponse, 0, sizeof(float) * samples);
}

void ConvolutionFilter::destroy() {
    delete[] m_shiftRegister;
    delete[] m_impulseResponse;

    m_shiftRegister = nullptr;
    m_impulseResponse = nullptr;
}

float ConvolutionFilter::f(float sample) {
    m_shiftRegister[m_shiftOffset] = sample;

    const int firstCount = m_sampleCount - m_shiftOffset;
    float result = 0.0f;
#if defined(__ANDROID__) && defined(__aarch64__)
    // ARM64 NEON is baseline on the Android ABI we ship. Vectorize the two
    // contiguous halves of the circular FIR instead of changing/truncating the
    // impulse response, preserving the exhaust character while reducing CPU.
    result += dotProductNeon(m_impulseResponse, m_shiftRegister + m_shiftOffset, firstCount);
    if (m_shiftOffset > 0) {
        result += dotProductNeon(m_impulseResponse + firstCount, m_shiftRegister, m_shiftOffset);
    }
#else
    for (int i = 0; i < firstCount; ++i) {
        result += m_impulseResponse[i] * m_shiftRegister[i + m_shiftOffset];
    }
    for (int i = firstCount; i < m_sampleCount; ++i) {
        result += m_impulseResponse[i] * m_shiftRegister[i - firstCount];
    }
#endif

    if (--m_shiftOffset < 0) m_shiftOffset = m_sampleCount - 1;
    return result;
}
