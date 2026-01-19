#include "engine_sim_runtime_node.h"

#include <godot_cpp/classes/audio_stream_generator.hpp>
#include <godot_cpp/classes/audio_stream_generator_playback.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <vector>

// Build version for debugging stale builds
#define ENGINE_SIM_BUILD_VERSION "2026-01-19-v3-10kHz-2fluid"

// HACK: Bypass es_runtime_c to access internal simulator directly
// and force audio sample rate to match Godot's mix rate.
#include <simulator.h>
struct es_runtime_mirror_t {
    Engine *engine;
    Vehicle *vehicle;
    Transmission *transmission;
    Simulator *simulator;
};
// End HACK

namespace godot {

EngineSimRuntime::EngineSimRuntime() {
    m_rt = es_runtime_create();
    UtilityFunctions::print(String("engine-sim: build ") + String(ENGINE_SIM_BUILD_VERSION));
    // Don't start the audio thread here - wait until after script is loaded
    // and synthesizer is initialized in load_mr_script()
}

EngineSimRuntime::~EngineSimRuntime() {
    stop_audio();

    if (m_rt != nullptr) {
        es_runtime_mirror_t *mirror = reinterpret_cast<es_runtime_mirror_t *>(m_rt);
        if (mirror && mirror->simulator) {
            mirror->simulator->endAudioRenderingThread();
        }

        es_runtime_destroy(m_rt);
        m_rt = nullptr;
    }
}

void EngineSimRuntime::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_mr_script", "path"), &EngineSimRuntime::load_mr_script);
    ClassDB::bind_method(D_METHOD("set_speed_control", "speed_control_0_to_1"), &EngineSimRuntime::set_speed_control);
    ClassDB::bind_method(D_METHOD("set_throttle", "throttle_0_to_1"), &EngineSimRuntime::set_throttle);
    ClassDB::bind_method(D_METHOD("get_throttle"), &EngineSimRuntime::get_throttle);
    ClassDB::bind_method(D_METHOD("set_starter_enabled", "enabled"), &EngineSimRuntime::set_starter_enabled);
    ClassDB::bind_method(D_METHOD("set_ignition_enabled", "enabled"), &EngineSimRuntime::set_ignition_enabled);

    // Transmission/clutch control
    ClassDB::bind_method(D_METHOD("set_gear", "gear"), &EngineSimRuntime::set_gear);
    ClassDB::bind_method(D_METHOD("get_gear"), &EngineSimRuntime::get_gear);
    ClassDB::bind_method(D_METHOD("get_gear_count"), &EngineSimRuntime::get_gear_count);
    ClassDB::bind_method(D_METHOD("set_clutch_pressure", "pressure_0_to_1"), &EngineSimRuntime::set_clutch_pressure);
    ClassDB::bind_method(D_METHOD("get_clutch_pressure"), &EngineSimRuntime::get_clutch_pressure);

    ClassDB::bind_method(D_METHOD("start_audio", "mix_rate", "buffer_length"), &EngineSimRuntime::start_audio);
    ClassDB::bind_method(D_METHOD("stop_audio"), &EngineSimRuntime::stop_audio);
    ClassDB::bind_method(D_METHOD("is_audio_running"), &EngineSimRuntime::is_audio_running);

    ClassDB::bind_method(D_METHOD("read_audio_stereo", "frames"), &EngineSimRuntime::read_audio_stereo);
    ClassDB::bind_method(D_METHOD("wait_audio_processed"), &EngineSimRuntime::wait_audio_processed);
    
    ClassDB::bind_method(D_METHOD("get_engine_speed"), &EngineSimRuntime::get_engine_speed);

    ClassDB::bind_method(D_METHOD("set_audio_debug_enabled", "enabled"), &EngineSimRuntime::set_audio_debug_enabled);
    ClassDB::bind_method(D_METHOD("is_audio_debug_enabled"), &EngineSimRuntime::is_audio_debug_enabled);
    ClassDB::bind_method(D_METHOD("set_audio_debug_interval", "seconds"), &EngineSimRuntime::set_audio_debug_interval);
    ClassDB::bind_method(D_METHOD("get_audio_debug_interval"), &EngineSimRuntime::get_audio_debug_interval);

    ClassDB::bind_method(D_METHOD("set_max_sim_steps_per_frame", "steps"), &EngineSimRuntime::set_max_sim_steps_per_frame);
    ClassDB::bind_method(D_METHOD("get_max_sim_steps_per_frame"), &EngineSimRuntime::get_max_sim_steps_per_frame);

    ClassDB::bind_method(D_METHOD("set_simulation_speed", "speed"), &EngineSimRuntime::set_simulation_speed);
    ClassDB::bind_method(D_METHOD("get_simulation_speed"), &EngineSimRuntime::get_simulation_speed);
}

void EngineSimRuntime::set_max_sim_steps_per_frame(int steps) {
    m_sim_steps_per_process = MAX(1, steps);
}

int EngineSimRuntime::get_max_sim_steps_per_frame() const {
    return m_sim_steps_per_process;
}

void EngineSimRuntime::set_simulation_speed(double speed) {
    if (m_rt == nullptr) {
        return;
    }
    es_runtime_set_simulation_speed(m_rt, speed);
}

double EngineSimRuntime::get_simulation_speed() const {
    if (m_rt == nullptr) {
        return 1.0;
    }
    return es_runtime_get_simulation_speed(m_rt);
}

void EngineSimRuntime::set_audio_debug_enabled(bool enabled) {
    m_audio_debug_enabled = enabled;
    m_audio_debug_accum_s = 0.0;
}

bool EngineSimRuntime::is_audio_debug_enabled() const {
    return m_audio_debug_enabled;
}

void EngineSimRuntime::set_audio_debug_interval(double seconds) {
    // Avoid spam and avoid long gaps.
    m_audio_debug_interval_s = CLAMP(seconds, 0.05, 10.0);
}

double EngineSimRuntime::get_audio_debug_interval() const {
    return m_audio_debug_interval_s;
}

bool EngineSimRuntime::load_mr_script(const String &path) {
    if (m_rt == nullptr) {
        return false;
    }

    const String abs_path = ProjectSettings::get_singleton()->globalize_path(path);
    const CharString utf8 = abs_path.utf8();

    const bool ok = es_runtime_load_script(m_rt, utf8.get_data());
    m_loaded = ok && es_runtime_has_simulation(m_rt);

    if (!m_loaded) {
        UtilityFunctions::printerr(String("engine-sim: failed to load script: ") + abs_path);
        return false;
    }

    // Audio rendering thread is already started by es_runtime_load_script()
    // No need to start it again here

    return m_loaded;
}

void EngineSimRuntime::set_speed_control(double speed_control_0_to_1) {
    if (m_rt == nullptr) {
        return;
    }

    es_runtime_set_speed_control(m_rt, speed_control_0_to_1);
}

void EngineSimRuntime::set_throttle(double throttle_0_to_1) {
    if (m_rt == nullptr) {
        return;
    }

    es_runtime_set_throttle(m_rt, throttle_0_to_1);
}

double EngineSimRuntime::get_throttle() const {
    if (m_rt == nullptr) {
        return 0.0;
    }

    return es_runtime_get_throttle(m_rt);
}

void EngineSimRuntime::set_starter_enabled(bool enabled) {
    if (m_rt == nullptr) {
        return;
    }

    es_runtime_set_starter_enabled(m_rt, enabled);
}

void EngineSimRuntime::set_ignition_enabled(bool enabled) {
    if (m_rt == nullptr) {
        return;
    }

    es_runtime_set_ignition_enabled(m_rt, enabled);
}

void EngineSimRuntime::set_gear(int gear) {
    if (m_rt == nullptr) {
        return;
    }
    es_runtime_set_gear(m_rt, gear);
}

int EngineSimRuntime::get_gear() const {
    if (m_rt == nullptr) {
        return 0;
    }
    return es_runtime_get_gear(m_rt);
}

int EngineSimRuntime::get_gear_count() const {
    if (m_rt == nullptr) {
        return 0;
    }
    return es_runtime_get_gear_count(m_rt);
}

void EngineSimRuntime::set_clutch_pressure(double pressure_0_to_1) {
    if (m_rt == nullptr) {
        return;
    }
    es_runtime_set_clutch_pressure(m_rt, pressure_0_to_1);
}

double EngineSimRuntime::get_clutch_pressure() const {
    if (m_rt == nullptr) {
        return 0.0;
    }
    return es_runtime_get_clutch_pressure(m_rt);
}

void EngineSimRuntime::start_audio(double mix_rate, double buffer_length) {
    if (mix_rate <= 0.0) {
        mix_rate = 44100.0;
    }
    if (buffer_length <= 0.0) {
        buffer_length = 0.3;
    }

    // Clamp to a practical range: lower values reduce latency but risk underruns.
    buffer_length = CLAMP(buffer_length, 0.1, 1.0);

    m_audio_mix_rate = mix_rate;
    m_audio_buffer_length = buffer_length;
    m_audio_buffer_capacity_frames = static_cast<int>(m_audio_mix_rate * m_audio_buffer_length);
    if (m_audio_buffer_capacity_frames < 1) {
        m_audio_buffer_capacity_frames = 1;
    }

    // Allow enough per-frame push budget to recover from hitching.
    m_audio_budget_frames = MAX(m_audio_budget_frames, m_audio_buffer_capacity_frames);

    if (m_audio_player == nullptr) {
        m_audio_player = memnew(AudioStreamPlayer);
        m_audio_player->set_name("EngineSimAudioPlayer");
        add_child(m_audio_player);
    }

    m_audio_generator.instantiate();
    m_audio_generator->set_mix_rate(mix_rate);
    m_audio_generator->set_buffer_length(buffer_length);

    m_audio_player->set_stream(m_audio_generator);

    // The synthesizer is now initialized at 44100 Hz in simulator.cpp,
    // so no re-initialization is needed here. This preserves the IR data.

    // Prefill: Run a few simulation frames to build up the synthesizer's internal buffer
    // before starting playback. This creates headroom so continuous consumption doesn't
    // cause immediate underruns.
    for (int i = 0; i < 3; ++i) {
        _physics_process(0.1);
        if (m_rt) {
            es_runtime_wait_audio_processed(m_rt);
        }
    }
    
    // Now start playback, which will immediately begin consuming audio.
    m_audio_player->play();

    Ref<AudioStreamPlayback> playback = m_audio_player->get_stream_playback();
    m_audio_playback = playback;
    if (m_audio_playback.is_null()) {
        UtilityFunctions::printerr("engine-sim: AudioStreamGeneratorPlayback unavailable (stream playback is null)");
        return;
    }

    // Godot may internally round buffer size; measure the actual capacity from playback.
    const int measured_capacity = m_audio_playback->get_frames_available();
    if (measured_capacity > 0) {
        m_audio_buffer_capacity_frames = measured_capacity;
    }
    
    // Transfer prefilled audio to Godot's buffer
    pump_audio();

    if (m_audio_debug_enabled) {
        UtilityFunctions::print(String("engine-sim[audio]: start mix_rate=") + String::num(m_audio_mix_rate)
            + String(" buffer_length=") + String::num(m_audio_buffer_length)
            + String(" capacity_frames=") + String::num_int64(m_audio_buffer_capacity_frames)
            + String(" chunk_frames=") + String::num_int64(m_audio_chunk_frames)
            + String(" budget_frames=") + String::num_int64(m_audio_budget_frames));
    }

    // Transfer the pre-filled audio from the synthesizer to Godot's buffer
    pump_audio();
}

void EngineSimRuntime::stop_audio() {
    if (m_audio_player != nullptr) {
        m_audio_player->stop();
    }

    m_audio_playback.unref();
    m_audio_generator.unref();

    m_audio_buffer_capacity_frames = 0;
}

bool EngineSimRuntime::is_audio_running() const {
    return m_audio_player != nullptr && m_audio_player->is_playing();
}

void EngineSimRuntime::_process(double delta) {
    // Pump audio every render frame (often faster than physics) to keep Godot's buffer fed.
    // This helps bridge gaps between physics frames when Godot's audio thread is consuming.
    pump_audio();
}

void EngineSimRuntime::_physics_process(double delta) {
    if (!m_loaded || m_rt == nullptr) {
        return;
    }

    if (!m_sim_frame_active) {
        // Accumulate any delta we missed if we had to split previous frames.
        double total_delta = delta + m_sim_accumulated_delta;
        
        // Safety clamp: Prevent ridiculous catch-up spikes if the app hangs for seconds.
        if (total_delta > 0.25) {
            total_delta = 0.25;
        }

        es_runtime_start_frame(m_rt, total_delta);
        m_sim_accumulated_delta = 0.0;
        m_sim_frame_active = true;
    } else {
        // We are continuing a split frame; accumulate this new delta for the next start_frame.
        m_sim_accumulated_delta += delta;
    }

    int steps = 0;
    bool frame_complete = false;
    while (steps < m_sim_steps_per_process) {
        if (!es_runtime_simulate_step(m_rt)) {
            frame_complete = true;
            break;
        }
        ++steps;
    }

    static int s_frameCount = 0;
    static int s_totalSteps = 0;
    s_totalSteps += steps;
    if (++s_frameCount % 60 == 0) {
        UtilityFunctions::print(String("engine-sim[sim]: steps_this_frame=") + String::num_int64(steps) 
            + String(" total_steps=") + String::num_int64(s_totalSteps)
            + String(" avg=") + String::num(s_totalSteps / 60.0)
            + String(" frame_complete=") + String(frame_complete ? "yes" : "no"));
        s_totalSteps = 0;
    }

    if (frame_complete) {
        es_runtime_end_frame(m_rt);
        m_sim_frame_active = false;
        
        // Don't wait for audio processing - let it run asynchronously
        // The audio buffer is large enough to handle timing variations
    }

    if (m_audio_debug_enabled) {
        m_audio_debug_accum_s += delta;
    }
}

void EngineSimRuntime::pump_audio() {
    if (m_rt == nullptr || m_audio_playback.is_null()) {
        return;
    }

    // Check current fill level - only pump if buffer drops below 50%
    const int capacity = m_audio_buffer_capacity_frames;
    const int frames_free = m_audio_playback->get_frames_available();
    const int frames_filled = capacity - frames_free;
    const float fill_ratio = (capacity > 0) ? (float)frames_filled / (float)capacity : 1.0f;
    
    // If buffer is above 50% full, let it drain a bit before pumping more
    // This prevents us from constantly draining the synth buffer
    if (fill_ratio > 0.5f) {
        return;
    }

    // Loop to fill Godot's buffer as much as possible
    int total_pushed = 0;
    const int chunk_size = 1024;
    const int max_iterations = 64;  // Cap iterations to avoid blocking too long
    
    for (int i = 0; i < max_iterations; ++i) {
        const int frames_available = m_audio_playback->get_frames_available();
        if (frames_available <= 0) {
            break;  // Godot's buffer is full
        }

        const int to_request = MIN(frames_available, chunk_size);

        // Read samples from the synthesizer
        m_audio_pcm16_tmp.resize(static_cast<size_t>(to_request));
        const int produced = es_runtime_read_audio(m_rt, to_request, m_audio_pcm16_tmp.data());
        
        if (produced <= 0) {
            break;  // No more audio available from synthesizer
        }

        // Convert and push
        PackedVector2Array stereo;
        stereo.resize(produced);
        Vector2 *w = stereo.ptrw();
        for (int j = 0; j < produced; ++j) {
            float s = static_cast<float>(m_audio_pcm16_tmp[static_cast<size_t>(j)]) / 32768.0f;
            w[j] = Vector2(s, s);
        }
        m_audio_playback->push_buffer(stereo);
        total_pushed += produced;

        // If synth gave us less than requested, it's drained - stop
        if (produced < to_request) {
            break;
        }
    }

    // Debug logging
    if (m_audio_debug_enabled && total_pushed > 0) {
        m_audio_debug_frames_pushed += static_cast<uint64_t>(total_pushed);
        
        if (m_audio_debug_accum_s >= m_audio_debug_interval_s) {
            m_audio_debug_accum_s = 0.0;
            
            const int capacity = m_audio_buffer_capacity_frames;
            const int frames_free_now = m_audio_playback->get_frames_available();
            const int frames_filled = (capacity > 0) ? (capacity - frames_free_now) : -1;
            const float fill_ratio = (float)frames_filled / (float)capacity;
            
            UtilityFunctions::print(String("engine-sim[audio]: filled=") + String::num_int64(frames_filled)
                + String(" (") + String::num(fill_ratio * 100.0, 1) + String("%)")
                + String(" pushed=") + String::num_int64(total_pushed));
        }
    }
}

PackedVector2Array EngineSimRuntime::read_audio_stereo(int frames) {
    PackedVector2Array out;

    if (frames <= 0 || m_rt == nullptr) {
        return out;
    }

    m_audio_pcm16_tmp.resize(static_cast<size_t>(frames));
    const int produced = es_runtime_read_audio(m_rt, frames, m_audio_pcm16_tmp.data());
    if (m_audio_debug_enabled) {
        m_audio_debug_frames_produced += static_cast<uint64_t>(MAX(produced, 0));
        if (produced < frames) {
            m_audio_debug_underrun_events += 1;
            // Log every shortfall to understand the pattern
            static int s_shortfallLogCount = 0;
            if (++s_shortfallLogCount <= 20) {
                UtilityFunctions::print(String("engine-sim[SHORTFALL]: requested=") + String::num_int64(frames)
                    + String(" got=") + String::num_int64(produced)
                    + String(" count=") + String::num_int64(m_audio_debug_underrun_events));
            }
        }
        
        // Detect discontinuities at chunk boundaries
        static int16_t last_sample = 0;
        static int jump_count = 0;
        if (produced > 0) {
            int16_t first_sample = m_audio_pcm16_tmp[0];
            int jump = std::abs(first_sample - last_sample);
            if (jump > 5000) {  // Large jump between chunks
                jump_count++;
                if (jump_count <= 10) {  // Only log first 10
                    UtilityFunctions::print(String("engine-sim[JUMP]: ") + String::num_int64(last_sample) 
                        + String(" -> ") + String::num_int64(first_sample)
                        + String(" delta=") + String::num_int64(jump));
                }
            }
            last_sample = m_audio_pcm16_tmp[produced - 1];
        }
        
        // Sample some audio values to check if they're valid
        static int debug_counter = 0;
        if (++debug_counter % 100 == 0 && produced > 0) {
            int16_t min_val = m_audio_pcm16_tmp[0];
            int16_t max_val = m_audio_pcm16_tmp[0];
            for (int i = 0; i < produced; ++i) {
                if (m_audio_pcm16_tmp[i] < min_val) min_val = m_audio_pcm16_tmp[i];
                if (m_audio_pcm16_tmp[i] > max_val) max_val = m_audio_pcm16_tmp[i];
            }
            double rpm = es_runtime_get_engine_speed(m_rt);
            UtilityFunctions::print(String("engine-sim[samples]: min=") + String::num_int64(min_val)
                + String(" max=") + String::num_int64(max_val)
                + String(" rpm=") + String::num(rpm, 1)
                + String(" count=") + String::num_int64(produced)
                + String(" jumps=") + String::num_int64(jump_count));
        }
    }

    if (produced <= 0) {
        return out;
    }

    out.resize(produced);
    Vector2 *w = out.ptrw();
    for (int i = 0; i < produced; ++i) {
        // Convert int16 to float [-1, 1] - no boost needed, leveler handles gain
        float s = static_cast<float>(m_audio_pcm16_tmp[static_cast<size_t>(i)]) / 32768.0f;
        w[i] = Vector2(s, s);
    }

    return out;
}

void EngineSimRuntime::wait_audio_processed() {
    if (m_rt == nullptr) {
        return;
    }

    es_runtime_wait_audio_processed(m_rt);
}

double EngineSimRuntime::get_engine_speed() const {
    if (m_rt == nullptr) {
        return 0.0;
    }
    return es_runtime_get_engine_speed(m_rt);
}

} // namespace godot
