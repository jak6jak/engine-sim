#include "../include/jitter_filter.h"

JitterFilter::JitterFilter() {
    m_history = nullptr;
    m_maxJitter = 0;
    m_offset = 0;
    m_jitterScale = 0.0f;
    m_rngState = 0x87654321;  // Non-zero seed
}

JitterFilter::~JitterFilter() {
    /* void */
}

void JitterFilter::initialize(
    int maxJitter,
    float cutoffFrequency,
    float audioFrequency)
{
    m_maxJitter = maxJitter;

    m_history = new float[maxJitter];
    m_offset = 0;
    memset(m_history, 0, sizeof(float) * maxJitter);

    m_noiseFilter.setCutoffFrequency(cutoffFrequency, audioFrequency);

    // Initialize RNG with unique seed based on address to make each filter different
    m_rngState = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)) ^ 0xABCDEF01;
    if (m_rngState == 0) m_rngState = 0x87654321;  // Ensure non-zero
}

float JitterFilter::f(float sample) {
    return fast_f(sample);
}
