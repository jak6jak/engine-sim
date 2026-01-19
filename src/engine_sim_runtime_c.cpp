#include "../include/engine_sim_runtime_c.h"

#include "../include/engine.h"
#include "../include/ignition_module.h"
#include "../include/piston_engine_simulator.h"
#include "../include/units.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef ATG_ENGINE_SIM_PIRANHA_ENABLED
#include "../scripting/include/compiler.h"
#endif

namespace {

struct WavData {
    std::vector<int16_t> mono_pcm16;
    int sample_rate = 0;
};

static uint16_t read_u16_le(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

static uint32_t read_u32_le(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24));
}

static bool read_file_bytes(const std::filesystem::path &path, std::vector<uint8_t> *out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0) return false;

    out->resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(out->data()), size);
    return file.good();
}

static bool parse_wav_mono_pcm16(const std::filesystem::path &path, WavData *out) {
    std::vector<uint8_t> bytes;
    if (!read_file_bytes(path, &bytes)) return false;
    if (bytes.size() < 44) return false;

    auto tag_eq = [&](size_t offset, const char a, const char b, const char c, const char d) {
        if (offset + 4 > bytes.size()) return false;
        return bytes[offset + 0] == static_cast<uint8_t>(a)
            && bytes[offset + 1] == static_cast<uint8_t>(b)
            && bytes[offset + 2] == static_cast<uint8_t>(c)
            && bytes[offset + 3] == static_cast<uint8_t>(d);
    };

    if (!tag_eq(0, 'R', 'I', 'F', 'F')) return false;
    if (!tag_eq(8, 'W', 'A', 'V', 'E')) return false;

    // Walk chunks
    size_t offset = 12;

    bool have_fmt = false;
    bool have_data = false;

    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;

    const uint8_t *data_ptr = nullptr;
    uint32_t data_size = 0;

    while (offset + 8 <= bytes.size()) {
        const uint32_t chunk_size = read_u32_le(bytes.data() + offset + 4);
        const size_t chunk_data = offset + 8;
        if (chunk_data + chunk_size > bytes.size()) return false;

        if (tag_eq(offset, 'f', 'm', 't', ' ')) {
            if (chunk_size < 16) return false;
            audio_format = read_u16_le(bytes.data() + chunk_data + 0);
            num_channels = read_u16_le(bytes.data() + chunk_data + 2);
            sample_rate = read_u32_le(bytes.data() + chunk_data + 4);
            bits_per_sample = read_u16_le(bytes.data() + chunk_data + 14);
            have_fmt = true;
        }
        else if (tag_eq(offset, 'd', 'a', 't', 'a')) {
            data_ptr = bytes.data() + chunk_data;
            data_size = chunk_size;
            have_data = true;
        }

        // Chunks are padded to even sizes.
        offset = chunk_data + chunk_size + (chunk_size % 2);
    }

    if (!have_fmt || !have_data) return false;
    if (num_channels == 0) return false;
    if (sample_rate == 0) return false;

    out->sample_rate = static_cast<int>(sample_rate);
    out->mono_pcm16.clear();

    const size_t bytes_per_sample = (bits_per_sample / 8);
    if (bytes_per_sample == 0) return false;

    const size_t frame_bytes = bytes_per_sample * num_channels;
    if (frame_bytes == 0) return false;

    const size_t frame_count = data_size / frame_bytes;
    if (frame_count == 0) return false;

    out->mono_pcm16.resize(frame_count);

    if (audio_format == 1 && bits_per_sample == 16) {
        // PCM16
        const auto *samples = reinterpret_cast<const int16_t *>(data_ptr);
        for (size_t i = 0; i < frame_count; ++i) {
            int32_t sum = 0;
            for (uint16_t ch = 0; ch < num_channels; ++ch) {
                sum += samples[i * num_channels + ch];
            }
            out->mono_pcm16[i] = static_cast<int16_t>(sum / static_cast<int32_t>(num_channels));
        }
        return true;
    }
    else if (audio_format == 3 && bits_per_sample == 32) {
        // IEEE float
        const auto *samples = reinterpret_cast<const float *>(data_ptr);
        for (size_t i = 0; i < frame_count; ++i) {
            double sum = 0.0;
            for (uint16_t ch = 0; ch < num_channels; ++ch) {
                sum += samples[i * num_channels + ch];
            }
            const double mono = sum / static_cast<double>(num_channels);
            const double clamped = std::max(-1.0, std::min(1.0, mono));
            out->mono_pcm16[i] = static_cast<int16_t>(std::lround(clamped * 32767.0));
        }
        return true;
    }

    return false;
}

static std::filesystem::path resolve_maybe_relative(
    const std::filesystem::path &base_dir,
    const std::filesystem::path &p)
{
    if (p.is_absolute()) return p;
    return base_dir / p;
}

static double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

} // namespace

struct es_runtime_t {
    Engine *engine = nullptr;
    Vehicle *vehicle = nullptr;
    Transmission *transmission = nullptr;
    Simulator *simulator = nullptr;

    std::filesystem::path base_dir;

    void clear() {
        if (simulator != nullptr) {
            simulator->destroy();
            delete simulator;
            simulator = nullptr;
        }

        if (engine != nullptr) {
            engine->destroy();
            delete engine;
            engine = nullptr;
        }

        if (vehicle != nullptr) {
            delete vehicle;
            vehicle = nullptr;
        }

        if (transmission != nullptr) {
            delete transmission;
            transmission = nullptr;
        }

        base_dir.clear();
    }
};

extern "C" {

es_runtime_t *es_runtime_create(void) {
    return new es_runtime_t;
}

void es_runtime_destroy(es_runtime_t *rt) {
    if (rt == nullptr) return;
    rt->clear();
    delete rt;
}

bool es_runtime_has_simulation(const es_runtime_t *rt) {
    return rt != nullptr && rt->simulator != nullptr && rt->engine != nullptr;
}

bool es_runtime_load_script(es_runtime_t *rt, const char *script_path) {
    if (rt == nullptr || script_path == nullptr) return false;

    rt->clear();

    rt->base_dir = std::filesystem::path(script_path).parent_path();

#ifdef ATG_ENGINE_SIM_PIRANHA_ENABLED
    Engine *engine = nullptr;
    Vehicle *vehicle = nullptr;
    Transmission *transmission = nullptr;
    Simulator::Parameters sim_params;

    {
        es_script::Compiler compiler;
        compiler.initialize();

        const bool compiled = compiler.compile(script_path);
        if (!compiled) {
            compiler.destroy();
            return false;
        }

        const es_script::Compiler::Output output = compiler.execute();
        compiler.destroy();

        engine = output.engine;
        vehicle = output.vehicle;
        transmission = output.transmission;
        sim_params = output.simulatorParameters;
    }

    if (engine == nullptr) {
        return false;
    }

    if (vehicle == nullptr) {
        Vehicle::Parameters vehParams;
        vehParams.mass = units::mass(1597, units::kg);
        vehParams.diffRatio = 3.42;
        vehParams.tireRadius = units::distance(10, units::inch);
        vehParams.dragCoefficient = 0.25;
        vehParams.crossSectionArea = units::distance(6.0, units::foot) * units::distance(6.0, units::foot);
        vehParams.rollingResistance = 2000.0;
        vehicle = new Vehicle;
        vehicle->initialize(vehParams);
    }

    if (transmission == nullptr) {
        static const double gearRatios[] = { 2.97, 2.07, 1.43, 1.00, 0.84, 0.56 };
        Transmission::Parameters tParams;
        tParams.GearCount = 6;
        tParams.GearRatios = gearRatios;
        tParams.MaxClutchTorque = units::torque(1000.0, units::ft_lb);
        transmission = new Transmission;
        transmission->initialize(tParams);
    }

    // Create simulator (full engine simulation) + synthesizer
    auto *sim = new PistonEngineSimulator;
    sim->initialize(sim_params);
    sim->setSimulationFrequency(engine->getSimulationFrequency());
    sim->loadSimulation(engine, vehicle, transmission);
    sim->setFluidSimulationSteps(8);

    engine->calculateDisplacement();

    // Copy initial audio parameters from engine (matching original engine-sim app)
    {
        Synthesizer::AudioParameters audioParams = sim->synthesizer().getAudioParameters();
        // Use engine's initial settings like the original app
        audioParams.inputSampleNoise = static_cast<float>(engine->getInitialJitter());
        audioParams.airNoise = static_cast<float>(engine->getInitialNoise());
        audioParams.dF_F_mix = static_cast<float>(engine->getInitialHighFrequencyGain());
        // Keep defaults for other params (from synthesizer.h):
        // volume = 1.0, convolution = 1.0, levelerTarget = 30000, levelerMaxGain = 100
        sim->synthesizer().setAudioParameters(audioParams);
        
        std::fprintf(stderr, "engine-sim: audioParams: volume=%.2f dF_F_mix=%.4f airNoise=%.2f convolution=%.2f levelerTarget=%.0f maxGain=%.1f\n",
            audioParams.volume, audioParams.dF_F_mix, audioParams.airNoise, audioParams.convolution, audioParams.levelerTarget, audioParams.levelerMaxGain);
    }

    // Load impulse responses without delta-studio
    for (int i = 0; i < engine->getExhaustSystemCount(); ++i) {
        ImpulseResponse *response = engine->getExhaustSystem(i)->getImpulseResponse();
        if (response == nullptr) {
            std::fprintf(stderr, "engine-sim: ExhaustSystem %d has no impulse response\n", i);
            continue;
        }
        const std::string filename = response->getFilename();
        if (filename.empty()) {
            std::fprintf(stderr, "engine-sim: ExhaustSystem %d impulse response has no filename\n", i);
            continue;
        }

        // Try multiple path resolutions:
        // 1. Relative to script dir
        // 2. In es/sound-library/ under script dir (for paths like "new/minimal_muffling_02.wav")
        // 3. Absolute path as-is
        std::vector<std::filesystem::path> candidates = {
            resolve_maybe_relative(rt->base_dir, filename),
            rt->base_dir / "es" / "sound-library" / filename,
            std::filesystem::path(filename)
        };

        bool loaded = false;
        for (const auto &ir_path : candidates) {
            std::filesystem::path normalized = ir_path.lexically_normal();
            std::fprintf(stderr, "engine-sim: Trying IR path: %s\n", normalized.c_str());
            
            WavData wav;
            if (parse_wav_mono_pcm16(normalized, &wav)) {
                float irVolume = static_cast<float>(response->getVolume());
                std::fprintf(stderr, "engine-sim: Loaded IR with %zu samples at %d Hz, volume=%.2f\n", 
                    wav.mono_pcm16.size(), wav.sample_rate, irVolume);
                sim->synthesizer().initializeImpulseResponse(
                    wav.mono_pcm16.data(),
                    static_cast<unsigned int>(wav.mono_pcm16.size()),
                    irVolume,
                    i);
                loaded = true;
                break;
            }
        }
        
        if (!loaded) {
            std::fprintf(stderr, "engine-sim: FAILED to load IR for ExhaustSystem %d\n", i);
        }
    }

    sim->startAudioRenderingThread();

    rt->engine = engine;
    rt->vehicle = vehicle;
    rt->transmission = transmission;
    rt->simulator = sim;

    return true;
#else
    (void)script_path;
    return false;
#endif
}

void es_runtime_set_speed_control(es_runtime_t *rt, double speed_control_0_to_1) {
    if (rt == nullptr || rt->engine == nullptr) return;
    rt->engine->setSpeedControl(clamp01(speed_control_0_to_1));
}

void es_runtime_set_throttle(es_runtime_t *rt, double throttle_0_to_1) {
    if (rt == nullptr || rt->engine == nullptr) return;
    rt->engine->setThrottle(clamp01(throttle_0_to_1));
}

double es_runtime_get_throttle(const es_runtime_t *rt) {
    if (rt == nullptr || rt->engine == nullptr) return 0.0;
    return rt->engine->getThrottle();
}

void es_runtime_set_starter_enabled(es_runtime_t *rt, bool enabled) {
    if (rt == nullptr || rt->simulator == nullptr) return;
    rt->simulator->m_starterMotor.m_enabled = enabled;
}

void es_runtime_set_ignition_enabled(es_runtime_t *rt, bool enabled) {
    if (rt == nullptr || rt->engine == nullptr) return;
    IgnitionModule *ignition = rt->engine->getIgnitionModule();
    if (ignition != nullptr) {
        ignition->m_enabled = enabled;
    }
}

void es_runtime_start_frame(es_runtime_t *rt, double dt_seconds) {
    if (rt == nullptr || rt->simulator == nullptr) return;
    rt->simulator->startFrame(dt_seconds);
}

bool es_runtime_simulate_step(es_runtime_t *rt) {
    if (rt == nullptr || rt->simulator == nullptr) return false;
    return rt->simulator->simulateStep();
}

void es_runtime_end_frame(es_runtime_t *rt) {
    if (rt == nullptr || rt->simulator == nullptr) return;
    rt->simulator->endFrame();
}

int es_runtime_read_audio(es_runtime_t *rt, int samples, int16_t *out_pcm16) {
    if (rt == nullptr || rt->simulator == nullptr || out_pcm16 == nullptr || samples <= 0) return 0;
    return rt->simulator->readAudioOutput(samples, out_pcm16);
}

void es_runtime_wait_audio_processed(es_runtime_t *rt) {
    if (rt == nullptr || rt->simulator == nullptr) return;
    rt->simulator->synthesizer().waitProcessed();
}

double es_runtime_get_engine_speed(es_runtime_t *rt) {
    if (rt == nullptr || rt->simulator == nullptr) return 0.0;
    return rt->simulator->filteredEngineSpeed();
}

} // extern "C"
