#ifndef ATG_ENGINE_SIM_ENGINE_SYNTHESIZER_H
#define ATG_ENGINE_SIM_ENGINE_SYNTHESIZER_H

#include "convolution_filter.h"
#include "leveling_filter.h"
#include "derivative_filter.h"
#include "low_pass_filter.h"
#include "jitter_filter.h"
#include "ring_buffer.h"
#include "butterworth_low_pass_filter.h"

#include <cinttypes>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

class Synthesizer {
    public:
        struct AudioParameters {
            float volume = 10.0f;
            float convolution = 1.0f;
            float dF_F_mix = 0.01f;
            float inputSampleNoise = 0.5f;
            float inputSampleNoiseFrequencyCutoff = 10000.0f;
            float airNoise = 1.0f;
            float airNoiseFrequencyCutoff = 2000.0f;
            float levelerTarget = 30000.0f;
            float levelerMaxGain = 100.0f;  // Allow much higher gain for quiet engines
            float levelerMinGain = 0.00001f;
        };

        struct Parameters {
            int inputChannelCount = 1;
            int inputBufferSize = 1024;
            int audioBufferSize = 44100;
            float inputSampleRate = 10000;
            float audioSampleRate = 44100;
            AudioParameters initialAudioParameters;
        };

        struct InputChannel {
            RingBuffer<float> data;
            float *transferBuffer = nullptr;
            double lastInputSample = 0.0f;
            double fractionalAccumulator = 0.0;
        };

        struct ProcessingFilters {
            ConvolutionFilter convolution;
            DerivativeFilter derivative;
            JitterFilter jitterFilter;
            ButterworthLowPassFilter<float> airNoiseLowPass;
            LowPassFilter inputDcFilter;
            ButterworthLowPassFilter<double> antialiasing;
        };

    public:
        Synthesizer();
        ~Synthesizer();

        void initialize(const Parameters &p);
        void initializeImpulseResponse(
            const int16_t *impulseResponse,
            unsigned int samples,
            float volume,
            int index);
        void startAudioRenderingThread();
        void endAudioRenderingThread();
        void destroy();

        int readAudioOutput(int samples, int16_t *buffer);

        void writeInput(const double *data);
        void writeInputBatch(const double *data);  // Alternative that may trigger more processing
        void endInputBlock();

        void waitProcessed();

        void audioRenderingThread();
        void renderAudio();

        double getLatency() const;

        int inputDelta(int s1, int s0) const;
        double inputDistance(double s1, double s0) const;

        void setInputSampleRate(double sampleRate);
        double getInputSampleRate() const { return m_inputSampleRate; }

        int16_t renderAudio(int inputOffset, const AudioParameters &params);

        double getLevelerGain();
        AudioParameters getAudioParameters();
        void setAudioParameters(const AudioParameters &params);

    //protected:
        ButterworthLowPassFilter<float> m_antialiasing;
        LevelingFilter m_levelingFilter;
        InputChannel *m_inputChannels;
        AudioParameters m_audioParameters;
        
        // Input batching support
        int m_batchInputCallCount = 0;
        static constexpr int BATCH_PROCESS_INTERVAL = 10;  // Process every 10 input calls
        int m_inputChannelCount;
        int m_inputBufferSize;
        int m_inputSamplesRead;
        int m_latency;
        double m_inputWriteOffset;
        double m_lastInputSampleOffset;

        RingBuffer<int16_t> m_audioBuffer;
        int m_audioBufferSize;

        float m_inputSampleRate;
        float m_audioSampleRate;

        std::thread *m_thread;
        std::atomic<bool> m_run;
        bool m_processed;
        bool m_singleThreaded = true;

        std::mutex m_lock0;
        std::condition_variable m_cv0;

        ProcessingFilters *m_filters;

        // Pre-allocated buffer for renderAudio to avoid hot-path allocation
        int16_t *m_renderBuffer;
        size_t m_renderBufferCapacity;

        // Fast PRNG state for audio synthesis (replaces slow rand())
        uint32_t m_rngState;

        // Fast xorshift32 PRNG - much faster than rand()
        inline float fastRandom() {
            m_rngState ^= m_rngState << 13;
            m_rngState ^= m_rngState >> 17;
            m_rngState ^= m_rngState << 5;
            return (m_rngState / 4294967296.0f) * 2.0f - 1.0f;  // Map to [-1, 1]
        }

};

#endif /* ATG_ENGINE_SIM_ENGINE_SYNTHESIZER_H */
