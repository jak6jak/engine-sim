#include "../include/synthesizer.h"

#include <cassert>
#include <cmath>

#undef min
#undef max

Synthesizer::Synthesizer() {
    m_inputChannels = nullptr;
    m_inputChannelCount = 0;
    m_inputBufferSize = 0;
    m_inputWriteOffset = 0.0;
    m_inputSamplesRead = 0;

    m_audioBufferSize = 0;

    m_inputSampleRate = 0.0;
    m_audioSampleRate = 0.0;

    m_lastInputSampleOffset = 0.0;

    m_run.store(false, std::memory_order_relaxed);
    m_thread = nullptr;
    m_filters = nullptr;
    m_renderBuffer = nullptr;
    m_renderBufferCapacity = 0;
    m_rngState = 0x12345678;  // Non-zero seed
}

Synthesizer::~Synthesizer() {
    assert(m_inputChannels == nullptr);
    assert(m_thread == nullptr);
    assert(m_filters == nullptr);
    assert(m_renderBuffer == nullptr);
}

void Synthesizer::initialize(const Parameters &p) {
    m_inputChannelCount = p.inputChannelCount;
    m_inputBufferSize = p.inputBufferSize;
    m_inputWriteOffset = p.inputBufferSize;
    m_audioBufferSize = p.audioBufferSize;
    m_inputSampleRate = p.inputSampleRate;
    m_audioSampleRate = p.audioSampleRate;
    m_audioParameters = p.initialAudioParameters;

    m_inputSamplesRead = 0;

    m_inputWriteOffset = 0;
    m_processed = true;

    m_audioBuffer.initialize(p.audioBufferSize);
    m_inputChannels = new InputChannel[p.inputChannelCount];
    for (int i = 0; i < p.inputChannelCount; ++i) {
        m_inputChannels[i].transferBuffer = new float[p.inputBufferSize];
        m_inputChannels[i].data.initialize(p.inputBufferSize);
        m_inputChannels[i].lastInputSample = 0.0;
        m_inputChannels[i].fractionalAccumulator = 0.0;
        std::fprintf(stderr, "engine-sim: InputChannel[%d] initialized: fractionalAccumulator=%.3f\n",
            i, m_inputChannels[i].fractionalAccumulator);
    }
     std::fprintf(stderr, "engine-sim: Synthesizer initialized with %d input channels, input buffer size=%d, audio buffer size=%d\n",
        p.inputChannelCount, p.inputBufferSize, p.audioBufferSize);
    std::fprintf(stderr, "engine-sim: Sample rates: input=%.1f audio=%.1f ratio=%.3f targetFill=%d\n",
        p.inputSampleRate, p.audioSampleRate, p.audioSampleRate / p.inputSampleRate, (p.audioBufferSize * 3) / 4);
    m_filters = new ProcessingFilters[p.inputChannelCount];
    for (int i = 0; i < p.inputChannelCount; ++i) {
        // Ensure convolution filter is always initialized so renderAudio() is safe
        // even when no impulse response WAV was loaded (pass-through).
        m_filters[i].convolution.initialize(1);
        m_filters[i].convolution.getImpulseResponse()[0] = 1.0f;

        m_filters[i].airNoiseLowPass.setCutoffFrequency(
            m_audioParameters.airNoiseFrequencyCutoff, m_audioSampleRate);

        m_filters[i].derivative.m_dt = 1 / m_audioSampleRate;

        m_filters[i].inputDcFilter.setCutoffFrequency(10.0);
        m_filters[i].inputDcFilter.m_dt = 1 / m_audioSampleRate;

        m_filters[i].jitterFilter.initialize(
            10,
            m_audioParameters.inputSampleNoiseFrequencyCutoff,
            m_audioSampleRate);

        m_filters[i].antialiasing.setCutoffFrequency(1900.0f, m_audioSampleRate);
    }

    m_levelingFilter.p_target = m_audioParameters.levelerTarget;
    m_levelingFilter.p_maxLevel = m_audioParameters.levelerMaxGain;
    m_levelingFilter.p_minLevel = m_audioParameters.levelerMinGain;
    m_antialiasing.setCutoffFrequency(m_audioSampleRate * 0.45f, m_audioSampleRate);

    for (int i = 0; i < m_audioBufferSize; ++i) {
        m_audioBuffer.write(0);
    }

    // Pre-allocate render buffer (max chunk size is 8192 in single-threaded mode)
    m_renderBufferCapacity = 8192;
    m_renderBuffer = new int16_t[m_renderBufferCapacity];
    std::fprintf(stderr, "engine-sim: Pre-allocated render buffer with capacity %zu\n", m_renderBufferCapacity);

    // Initialize fast PRNG with time-based seed
    m_rngState = static_cast<uint32_t>(std::time(nullptr)) ^ 0xDEADBEEF;
}

void Synthesizer::initializeImpulseResponse(
    const int16_t *impulseResponse,
    unsigned int samples,
    float volume,
    int index)
{
    unsigned int clippedLength = 0;
    for (unsigned int i = 0; i < samples; ++i) {
        if (std::abs(impulseResponse[i]) > 100) {
            clippedLength = i + 1;
        }
    }

    // Limit IR length for real-time performance. 4000 samples at 44.1kHz = ~90ms reverb.
    const unsigned int sampleCount = std::min(4000U, clippedLength);
    
    std::fprintf(stderr, "engine-sim: IR index=%d clippedLen=%u usedLen=%u volume=%.4f\n",
        index, clippedLength, sampleCount, volume);

    // Destroy existing convolution filter before re-initializing
    if (m_filters[index].convolution.getSampleCount() > 0) {
        m_filters[index].convolution.destroy();
    }

    m_filters[index].convolution.initialize(sampleCount);
    for (unsigned int i = 0; i < sampleCount; ++i) {
        m_filters[index].convolution.getImpulseResponse()[i] =
            volume * impulseResponse[i] / INT16_MAX;
    }
}

void Synthesizer::startAudioRenderingThread() {
    m_singleThreaded = true;
    m_run.store(true, std::memory_order_release);
}

void Synthesizer::endAudioRenderingThread() {
    m_run.store(false, std::memory_order_release);
    // Wake up any waiting threads so they can exit
    m_cv0.notify_all();
}

void Synthesizer::destroy() {
    m_audioBuffer.destroy();

    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.destroy();
        m_filters[i].convolution.destroy();
    }

    delete[] m_inputChannels;
    delete[] m_filters;
    delete[] m_renderBuffer;

    m_inputChannels = nullptr;
    m_filters = nullptr;
    m_renderBuffer = nullptr;
    m_renderBufferCapacity = 0;

    m_inputChannelCount = 0;
}

int Synthesizer::readAudioOutput(int samples, int16_t *buffer) {
    if (m_singleThreaded && samples > 0) {
        // Always generate audio at output sample rate, not waiting for large input buffers
        // Target just enough for the current request to avoid gaps
        const int targetBuffered = samples + 1024;  // Request size + small safety margin

        int inputBefore, audioBefore;
        {
            std::lock_guard<std::mutex> lk(m_lock0);
            inputBefore = (int)m_inputChannels[0].data.size();
            audioBefore = (int)m_audioBuffer.size();
        }

        // Process aggressively to ensure we hit the target each call
        // This maintains steady output even with slow input
        int processed = 0;
        int noInputCount = 0;
        for (int iterations = 0; iterations < 100000; ++iterations) {
            int inputAvailable, audioSize;
            {
                std::lock_guard<std::mutex> lk(m_lock0);
                inputAvailable = (int)m_inputChannels[0].data.size();
                audioSize = (int)m_audioBuffer.size();
            }

            if (audioSize >= targetBuffered) {
                break;
            }
            
            // If input is empty, allow a few renders using held samples, then give up
            if (inputAvailable == 0) {
                noInputCount++;
                if (noInputCount > 5) {  // Allow up to 5 renders with held samples
                    break;
                }
            } else {
                noInputCount = 0;  // Reset counter when input arrives
            }

            renderAudio();
            processed++;
        }

        static int logCounter = 0;
        if (++logCounter % 100 == 0 || audioBefore < samples) {
            int inputAfter, audioAfter;
            {
                std::lock_guard<std::mutex> lk(m_lock0);
                inputAfter = (int)m_inputChannels[0].data.size();
                audioAfter = (int)m_audioBuffer.size();
            }
            std::fprintf(stderr, "engine-sim[readAudio]: #%d req=%d inB=%d inA=%d audB=%d audA=%d proc=%d target=%d\n",
                logCounter, samples, inputBefore, inputAfter, audioBefore, audioAfter, processed, targetBuffered);
        }
    }

    int newDataLength;
    {
        std::lock_guard<std::mutex> lk(m_lock0);
        newDataLength = m_audioBuffer.size();
        if (newDataLength >= samples) {
            m_audioBuffer.readAndRemove(samples, buffer);
        }
        else {
            m_audioBuffer.readAndRemove(newDataLength, buffer);
        }
    }


    const int samplesConsumed = std::min(samples, newDataLength);

    return samplesConsumed;
}

void Synthesizer::waitProcessed() {
    if (m_singleThreaded) {
        return;
    }
    // Wait briefly for the render thread to process any pending input.
    // Use timed wait to avoid deadlock if render thread is busy.
    std::unique_lock<std::mutex> lk(m_lock0);
    m_cv0.wait_for(lk, std::chrono::milliseconds(50), [this] { 
        return m_processed || !m_run.load(std::memory_order_acquire); 
    });
}

void Synthesizer::writeInput(const double *data) {
    std::lock_guard<std::mutex> lk(m_lock0);

    // Calculate how many samples to generate based on sample rate ratio
    // This accumulates over time to avoid losing fractional samples
    double samplesToAdd = (double)m_audioSampleRate / m_inputSampleRate;
    
    m_inputWriteOffset += samplesToAdd;
    if (m_inputWriteOffset >= (double)m_inputBufferSize) {
        m_inputWriteOffset -= (double)m_inputBufferSize;
    }

    const double distance = inputDistance(m_inputWriteOffset, m_lastInputSampleOffset);

    for (int i = 0; i < m_inputChannelCount; ++i) {
        RingBuffer<float> &buffer = m_inputChannels[i].data;
        const double lastInputSample = m_inputChannels[i].lastInputSample;

        // Accumulate fractional samples to preserve sample rate ratio over time
        // If ratio is 2.205, this ensures we average 2.205 samples per input over time
        const double oldAccum = m_inputChannels[i].fractionalAccumulator;
        double samplesToGenerate = distance + oldAccum;
        int wholeSamples = (int)std::floor(samplesToGenerate);
        m_inputChannels[i].fractionalAccumulator = samplesToGenerate - wholeSamples;

        // Write the integer number of samples with linear interpolation
        // Distribute wholeSamples evenly across the distance
        if (wholeSamples > 0) {
            for (int j = 0; j < wholeSamples; j++) {
                // Interpolate evenly across wholeSamples, not distance
                const double f = (j + 0.5) / wholeSamples;
                const double sample = lastInputSample * (1 - f) + data[i] * f;

                buffer.write(m_filters[i].antialiasing.fast_f(static_cast<float>(sample)));
            }
        }

        static int writeLogCounter = 0;
        static int samplesWrittenTotal = 0;
        static int callsTotal = 0;
        samplesWrittenTotal += wholeSamples;
        callsTotal++;
        if (++writeLogCounter % 503 == 0) {  // More frequent logging to catch issues
            std::fprintf(stderr, "engine-sim[writeInput][ch%d]: distance=%.3f oldAcc=%.3f sampGen=%.3f wrote=%d newAcc=%.3f bufSize=%zu avgRatio=%.4f (%d calls, %d samples total)\n",
                i, distance, oldAccum, samplesToGenerate, wholeSamples, m_inputChannels[i].fractionalAccumulator, buffer.size(),
                (double)samplesWrittenTotal / callsTotal, callsTotal, samplesWrittenTotal);
        }

        m_inputChannels[i].lastInputSample = data[i];
    }

    m_lastInputSampleOffset = m_inputWriteOffset;
}

void Synthesizer::endInputBlock() {
    {
        std::lock_guard<std::mutex> lk(m_lock0);
        if (m_inputChannelCount != 0) {
            m_latency = m_inputChannels[0].data.size();
        }
        m_processed = false;
    }

    if (m_singleThreaded) {
        // Don't process here - let readAudioOutput handle all processing
        // Just mark as processed
        {
            std::lock_guard<std::mutex> lk(m_lock0);
            m_processed = true;
        }
    }
    else {
        m_cv0.notify_one();
    }
}

void Synthesizer::writeInputBatch(const double *data) {
    // Same as writeInput but tracks calls and can trigger more frequent processing
    writeInput(data);
    
    // Every N calls, notify the render thread to process
    // This ensures we don't wait for endInputBlock() to trigger audio generation
    m_batchInputCallCount++;
    if (m_batchInputCallCount >= BATCH_PROCESS_INTERVAL && !m_singleThreaded) {
        m_batchInputCallCount = 0;
        m_cv0.notify_one();  // Wake render thread to process accumulated input
    }
}

void Synthesizer::audioRenderingThread() {
    while (m_run.load(std::memory_order_acquire)) {
        renderAudio();
    }
}

#undef max
void Synthesizer::renderAudio() {
    // Process in reasonable chunks to avoid excessive iterations
    // Larger chunks in single-threaded to reduce overhead
    const int maxChunkSize = m_singleThreaded ? 8192 : 2000;
    const int minChunkSize = m_singleThreaded ? 512 : 128;  // Minimum to maintain smooth output

    // Determine how many samples to process and read audio parameters atomically
    int n;
    AudioParameters params;
    {
        std::lock_guard<std::mutex> lk(m_lock0);

        const int inputAvailable = (int)m_inputChannels[0].data.size();
        const int audioSize = (int)m_audioBuffer.size();
        const int audioSpaceLeft = std::max(0, m_audioBufferSize - audioSize - 1000);
        
        // Always try to render at least minChunkSize to keep audio flowing
        // When input runs out, we'll hold the last sample
        int actualInput = std::min({inputAvailable, maxChunkSize});
        n = std::max(minChunkSize, actualInput);  // At least minChunkSize, or more if input available
        n = std::min(n, audioSpaceLeft);           // But don't exceed buffer space

        if (n <= 0) {
            m_processed = true;
            if (!m_singleThreaded) {
                m_cv0.notify_one();
            }
            return;
        }

        // Read audio parameters under lock to avoid data race
        params = m_audioParameters;

        // Read input data while holding the lock
        for (int i = 0; i < m_inputChannelCount; ++i) {
            int readAvailable = std::min(actualInput, (int)m_inputChannels[i].data.size());
            m_inputChannels[i].data.readAndRemove(readAvailable, m_inputChannels[i].transferBuffer);
            
            // If we need more samples than available, hold the last sample value
            if (readAvailable < n) {
                // Use the last sample from what we read, or use the stored last value
                float holdSample = (readAvailable > 0) 
                    ? m_inputChannels[i].transferBuffer[readAvailable - 1]
                    : (float)m_inputChannels[i].lastInputSample;
                
                // Fill the rest with the held sample
                for (int j = readAvailable; j < n; ++j) {
                    m_inputChannels[i].transferBuffer[j] = holdSample;
                }
            }
        }

        static int logCounter = 0;
        static int holdEvents = 0;
        int inputRead = std::min(actualInput, (int)m_inputChannels[0].data.size());
        if (inputRead < n) {
            holdEvents++;
        }
        if (++logCounter % 50 == 0) {
            std::fprintf(stderr, "engine-sim[render]: inputAvail=%d input_read=%d audioSize=%d spaceLeft=%d rendering=%d (hold_events=%d)\n",
                inputAvailable, inputRead, audioSize, audioSpaceLeft, n, holdEvents);
        }
        // Don't set m_processed here - we haven't written to audio buffer yet
    }

    // Update filter parameters using the safely-read parameters
    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_filters[i].airNoiseLowPass.setCutoffFrequency(
            static_cast<float>(params.airNoiseFrequencyCutoff), m_audioSampleRate);
        m_filters[i].jitterFilter.setJitterScale(params.inputSampleNoise);
    }

    // Render audio samples (CPU-intensive, done outside lock)
    // Use pre-allocated buffer instead of vector allocation
    for (int i = 0; i < n; ++i) {
        m_renderBuffer[i] = renderAudio(i, params);
    }

    // Write rendered samples to output buffer and mark as processed
    {
        std::lock_guard<std::mutex> lk(m_lock0);
        for (int i = 0; i < n; ++i) {
            m_audioBuffer.write(m_renderBuffer[i]);
        }
        // Set m_processed after all work is complete
        m_processed = true;
    }

    // Notify waiting threads after releasing the lock
    if (!m_singleThreaded) {
        m_cv0.notify_one();
    }
}

double Synthesizer::getLatency() const {
    return (double)m_latency / m_audioSampleRate;
}

int Synthesizer::inputDelta(int s1, int s0) const {
    return (s1 < s0)
        ? m_inputBufferSize - s0 + s1
        : s1 - s0;
}

double Synthesizer::inputDistance(double s1, double s0) const {
    return (s1 < s0)
        ? (double)m_inputBufferSize - s0 + s1
        : s1 - s0;
}

void Synthesizer::setInputSampleRate(double sampleRate) {
    if (sampleRate != m_inputSampleRate) {
        std::lock_guard<std::mutex> lock(m_lock0);
        m_inputSampleRate = sampleRate;
    }
}

int16_t Synthesizer::renderAudio(int inputSample, const AudioParameters &params) {
    const float airNoise = params.airNoise;
    const float dF_F_mix = params.dF_F_mix;
    const float convAmount = params.convolution;

    float signal = 0;
    for (int i = 0; i < m_inputChannelCount; ++i) {
        // Use fast PRNG instead of slow rand()
        // const float r_0 = fastRandom();  // Currently unused

        const float jitteredSample =
            m_filters[i].jitterFilter.fast_f(m_inputChannels[i].transferBuffer[inputSample]);

        const float f_in = jitteredSample;
        const float f_dc = m_filters[i].inputDcFilter.fast_f(f_in);
        const bool bypassInputDc =
            params.inputSampleNoise == 0.0f
            && params.airNoise == 0.0f
            && params.dF_F_mix == 0.0f;
        const float f = bypassInputDc ? f_in : (f_in - f_dc);
        const float f_p = m_filters[i].derivative.f(f_in);

        // Use fast PRNG instead of slow rand()
        const float noise = fastRandom();
        const float r =
            m_filters->airNoiseLowPass.fast_f(noise);
        const float r_mixed =
            airNoise * r + (1 - airNoise);

        float v_in =
            f_p * dF_F_mix
            + f * r_mixed * (1 - dF_F_mix);
        if (fpclassify(v_in) == FP_SUBNORMAL) {
            v_in = 0;
        }

        const float v =
            convAmount * m_filters[i].convolution.f(v_in)
            + (1 - convAmount) * v_in;

        signal += v;
    }

    signal = m_antialiasing.fast_f(signal);

    m_levelingFilter.p_target = params.levelerTarget;
    m_levelingFilter.p_maxLevel = params.levelerMaxGain;
    m_levelingFilter.p_minLevel = params.levelerMinGain;
    const float v_leveled = m_levelingFilter.f(signal) * params.volume;
    int r_int = std::lround(v_leveled);
    if (r_int > INT16_MAX) {
        r_int = INT16_MAX;
    }
    else if (r_int < INT16_MIN) {
        r_int = INT16_MIN;
    }

    return static_cast<int16_t>(r_int);
}

double Synthesizer::getLevelerGain() {
    std::lock_guard<std::mutex> lock(m_lock0);
    return m_levelingFilter.getAttenuation();
}

Synthesizer::AudioParameters Synthesizer::getAudioParameters() {
    std::lock_guard<std::mutex> lock(m_lock0);
    return m_audioParameters;
}

void Synthesizer::setAudioParameters(const AudioParameters &params) {
    std::lock_guard<std::mutex> lock(m_lock0);
    m_audioParameters = params;
}
