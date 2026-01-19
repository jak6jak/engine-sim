#ifndef ATG_ENGINE_SIM_RUNTIME_C_H
#define ATG_ENGINE_SIM_RUNTIME_C_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
    #if defined(ENGINE_SIM_RUNTIME_EXPORTS)
        #define ES_RUNTIME_API __declspec(dllexport)
    #else
        #define ES_RUNTIME_API __declspec(dllimport)
    #endif
#else
    #define ES_RUNTIME_API __attribute__((visibility("default")))
#endif

typedef struct es_runtime_t es_runtime_t;

ES_RUNTIME_API es_runtime_t *es_runtime_create(void);
ES_RUNTIME_API void es_runtime_destroy(es_runtime_t *rt);

// Loads an Engine/Vehicle/Transmission from a .mr script (piranha).
// Returns false if PIRANHA_ENABLED is OFF, compilation fails, or output is missing required objects.
ES_RUNTIME_API bool es_runtime_load_script(es_runtime_t *rt, const char *script_path);

// Simulation controls
ES_RUNTIME_API bool es_runtime_has_simulation(const es_runtime_t *rt);
ES_RUNTIME_API void es_runtime_set_speed_control(es_runtime_t *rt, double speed_control_0_to_1);
ES_RUNTIME_API void es_runtime_set_throttle(es_runtime_t *rt, double throttle_0_to_1);  // Direct throttle control
ES_RUNTIME_API double es_runtime_get_throttle(const es_runtime_t *rt);  // Get current throttle position
ES_RUNTIME_API void es_runtime_set_starter_enabled(es_runtime_t *rt, bool enabled);
ES_RUNTIME_API void es_runtime_set_ignition_enabled(es_runtime_t *rt, bool enabled);  // Enable/disable spark

// Frame stepping: call in your game loop.
ES_RUNTIME_API void es_runtime_start_frame(es_runtime_t *rt, double dt_seconds);
ES_RUNTIME_API bool es_runtime_simulate_step(es_runtime_t *rt);  // returns false when frame complete
ES_RUNTIME_API void es_runtime_end_frame(es_runtime_t *rt);

// Audio: call from your audio thread/callback.
// Reads up to `samples` PCM16 samples; returns how many were available (the remainder is zero-filled).
ES_RUNTIME_API int es_runtime_read_audio(es_runtime_t *rt, int samples, int16_t *out_pcm16);

// Optional: blocks until the synthesizer thread processes the most recent input block.
ES_RUNTIME_API void es_runtime_wait_audio_processed(es_runtime_t *rt);

// Engine state queries
ES_RUNTIME_API double es_runtime_get_engine_speed(es_runtime_t *rt);  // Returns engine RPM

#ifdef __cplusplus
}
#endif

#endif /* ATG_ENGINE_SIM_RUNTIME_C_H */
