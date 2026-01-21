#include "../include/convolution_filter.h"

#include <assert.h>
#include <string.h>

// Temporarily disabled to test scalar fallback
#if (defined(__ARM_NEON) || defined(__ARM_NEON__))
#include <arm_neon.h>
#define USE_NEON 1
#endif

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

    float result = 0;

#if USE_NEON
    // NEON SIMD: process 4 floats at a time
    float32x4_t sum_vec = vdupq_n_f32(0.0f);
    
    // First segment: from m_shiftOffset to end (no wraparound)
    const int firstLoopEnd = m_sampleCount - m_shiftOffset;
    
    int i = 0;
    for (; i + 4 <= firstLoopEnd; i += 4) {
        float32x4_t ir_vec = vld1q_f32(m_impulseResponse + i);
        float32x4_t sr_vec = vld1q_f32(m_shiftRegister + i + m_shiftOffset);
        sum_vec = vmlaq_f32(sum_vec, ir_vec, sr_vec);
    }
    // Remaining samples in first segment (scalar)
    for (; i < firstLoopEnd; ++i) {
        result += m_impulseResponse[i] * m_shiftRegister[i + m_shiftOffset];
    }
    
    // Second segment: wraparound from beginning
    for (; i + 4 <= m_sampleCount; i += 4) {
        const int base = i - firstLoopEnd;
        float32x4_t ir_vec = vld1q_f32(m_impulseResponse + i);
        float32x4_t sr_vec = vld1q_f32(m_shiftRegister + base);
        sum_vec = vmlaq_f32(sum_vec, ir_vec, sr_vec);
    }
    // Remaining samples in second segment (scalar)
    for (; i < m_sampleCount; ++i) {
        result += m_impulseResponse[i] * m_shiftRegister[i - firstLoopEnd];
    }
    
    // Horizontal sum of NEON vector
    result += vaddvq_f32(sum_vec);
    
#else
    // Scalar fallback with loop unrolling
    const int firstLoopEnd = m_sampleCount - m_shiftOffset;
    const int unroll = 4;

    int i = 0;
    for (; i + unroll <= firstLoopEnd; i += unroll) {
        result += m_impulseResponse[i] * m_shiftRegister[i + m_shiftOffset];
        result += m_impulseResponse[i+1] * m_shiftRegister[i+1 + m_shiftOffset];
        result += m_impulseResponse[i+2] * m_shiftRegister[i+2 + m_shiftOffset];
        result += m_impulseResponse[i+3] * m_shiftRegister[i+3 + m_shiftOffset];
    }
    for (; i < firstLoopEnd; ++i) {
        result += m_impulseResponse[i] * m_shiftRegister[i + m_shiftOffset];
    }

    for (; i + unroll <= m_sampleCount; i += unroll) {
        const int base = i - (m_sampleCount - m_shiftOffset);
        result += m_impulseResponse[i] * m_shiftRegister[base];
        result += m_impulseResponse[i+1] * m_shiftRegister[base+1];
        result += m_impulseResponse[i+2] * m_shiftRegister[base+2];
        result += m_impulseResponse[i+3] * m_shiftRegister[base+3];
    }
    for (; i < m_sampleCount; ++i) {
        result += m_impulseResponse[i] * m_shiftRegister[i - (m_sampleCount - m_shiftOffset)];
    }
#endif

    // Replace expensive modulo with conditional
    if (--m_shiftOffset < 0) {
        m_shiftOffset = m_sampleCount - 1;
    }

    return result;
}
