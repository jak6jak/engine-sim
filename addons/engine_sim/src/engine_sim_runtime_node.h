#ifndef ENGINE_SIM_RUNTIME_NODE_H
#define ENGINE_SIM_RUNTIME_NODE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include <vector>

#include "engine_sim_runtime_c.h"

namespace godot {

class AudioStreamGenerator;
class AudioStreamGeneratorPlayback;
class AudioStreamPlayer;

class EngineSimRuntime : public Node {
    GDCLASS(EngineSimRuntime, Node)

public:
    EngineSimRuntime();
    ~EngineSimRuntime();

    bool load_mr_script(const String &path);
    void set_speed_control(double speed_control_0_to_1);
    void set_throttle(double throttle_0_to_1);  // Direct throttle control (0=closed, 1=wide open)
    double get_throttle() const;  // Get current throttle position
    void set_starter_enabled(bool enabled);
    void set_ignition_enabled(bool enabled);  // Enable spark plugs

    // Transmission/clutch control
    void set_gear(int gear);  // -1=reverse, 0=neutral, 1-N=forward gears
    int get_gear() const;
    int get_gear_count() const;  // Number of forward gears
    void set_clutch_pressure(double pressure_0_to_1);  // 0=disengaged, 1=fully engaged
    double get_clutch_pressure() const;

    void start_audio(double mix_rate = 44100.0, double buffer_length = 0.1);
    void stop_audio();
    bool is_audio_running() const;

    void set_audio_debug_enabled(bool enabled);
    bool is_audio_debug_enabled() const;
    void set_audio_debug_interval(double seconds);
    double get_audio_debug_interval() const;

    void set_max_sim_steps_per_frame(int steps);
    int get_max_sim_steps_per_frame() const;

    void set_simulation_speed(double speed);
    double get_simulation_speed() const;

    PackedVector2Array read_audio_stereo(int frames);
    void wait_audio_processed();
    
    double get_engine_speed() const;

    void _notification(int p_what);
    void _process(double delta) override;
    void _physics_process(double delta) override;

protected:
    static void _bind_methods();

private:
    void pump_audio();
    AudioStreamPlayer *get_audio_player() const;

    es_runtime_t *m_rt = nullptr;
    bool m_loaded = false;

    ObjectID m_audio_player_id;
    Ref<AudioStreamGenerator> m_audio_generator;
    Ref<AudioStreamGeneratorPlayback> m_audio_playback;

    double m_audio_mix_rate = 44100.0;
    double m_audio_buffer_length = 0.3;
    int m_audio_buffer_capacity_frames = 0;

    std::vector<int16_t> m_audio_pcm16_tmp;

    int m_audio_chunk_frames = 128;  // Smaller chunks to avoid shortfalls
    int m_audio_budget_frames = 16384;
    bool m_sim_frame_active = false;
    // Default to a high limit (approx 2s at 10kHz) so we normally finish frames in one go.
    // Splitting frames causes simulation time to drift from real time, causing audio underruns.
    int m_sim_steps_per_process = 50000;
    double m_sim_accumulated_delta = 0.0;

    bool m_audio_debug_enabled = false;
    double m_audio_debug_interval_s = 1.0;
    double m_audio_debug_accum_s = 0.0;
    uint64_t m_audio_debug_underrun_events = 0;
    uint64_t m_audio_debug_frames_pushed = 0;
    uint64_t m_audio_debug_frames_requested = 0;
    uint64_t m_audio_debug_frames_produced = 0;

    uint64_t m_audio_debug_last_frames_requested = 0;
    uint64_t m_audio_debug_last_frames_produced = 0;
};

} // namespace godot

#endif
