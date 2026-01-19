#include "../include/convolution_filter.h"

#include <assert.h>
#include <string.h>

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

    // Optimized convolution with loop unrolling by 4
    const int firstLoopEnd = m_sampleCount - m_shiftOffset;
    const int unroll = 4;

    // First segment: no wraparound needed
    int i = 0;
    for (; i + unroll <= firstLoopEnd; i += unroll) {
        result += m_impulseResponse[i] * m_shiftRegister[i + m_shiftOffset];
        result += m_impulseResponse[i+1] * m_shiftRegister[i+1 + m_shiftOffset];
        result += m_impulseResponse[i+2] * m_shiftRegister[i+2 + m_shiftOffset];
        result += m_impulseResponse[i+3] * m_shiftRegister[i+3 + m_shiftOffset];
    }
    // Remaining samples in first segment
    for (; i < firstLoopEnd; ++i) {
        result += m_impulseResponse[i] * m_shiftRegister[i + m_shiftOffset];
    }

    // Second segment: wraparound (typically short for 4000-sample IR)
    const int wrapBase = i - (m_sampleCount - m_shiftOffset);
    for (; i + unroll <= m_sampleCount; i += unroll) {
        const int base = i - (m_sampleCount - m_shiftOffset);
        result += m_impulseResponse[i] * m_shiftRegister[base];
        result += m_impulseResponse[i+1] * m_shiftRegister[base+1];
        result += m_impulseResponse[i+2] * m_shiftRegister[base+2];
        result += m_impulseResponse[i+3] * m_shiftRegister[base+3];
    }
    // Remaining samples in second segment
    for (; i < m_sampleCount; ++i) {
        result += m_impulseResponse[i] * m_shiftRegister[i - (m_sampleCount - m_shiftOffset)];
    }

    // Replace expensive modulo with conditional (much faster)
    if (--m_shiftOffset < 0) {
        m_shiftOffset = m_sampleCount - 1;
    }

    return result;
}
