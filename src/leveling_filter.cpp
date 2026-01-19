#include "../include/leveling_filter.h"

#include <cmath>
#include <cstdio>

LevelingFilter::LevelingFilter() {
    m_peak = 30000.0;
    m_attenuation = 1.0;
    p_target = 30000.0;
    p_minLevel = 0.0;
    p_maxLevel = 1.0;
    m_debugCounter = 0;
    m_lastReportedAtten = 1.0;
}

LevelingFilter::~LevelingFilter() {
    /* void */
}

float LevelingFilter::f(float sample) {
    // Slower decay to prevent audio from fading out during timing gaps.
    // 0.99999 at 44.1kHz means ~2 seconds to decay to ~37% (vs ~22ms with 0.999)
    m_peak = 0.99999f * m_peak;
    if (std::abs(sample) > m_peak) {
        m_peak = std::abs(sample);
    }

    if (m_peak == 0) return 0;

    const float raw_attenuation = p_target / m_peak;

    float attenuation = raw_attenuation;
    if (attenuation < p_minLevel) attenuation = p_minLevel;
    else if (attenuation > p_maxLevel) attenuation = p_maxLevel;

    // Much slower smoothing to avoid audible gain pumping/clicking
    // 0.999/0.001 at 44.1kHz means ~700 samples (~16ms) to reach 50% of target
    // This prevents the sudden 1-2% jumps that cause blips
    m_attenuation = 0.999f * m_attenuation + 0.001f * attenuation;

    return sample * m_attenuation;
}
