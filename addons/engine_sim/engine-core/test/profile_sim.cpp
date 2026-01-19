// Profiling test for engine simulation
// Run with: xcrun xctrace record --template 'Time Profiler' --launch ./build/engine-sim-test
// Then open the .trace file in Instruments

#include <gtest/gtest.h>
#include "../include/engine_sim_runtime_c.h"
#include <chrono>
#include <cstdio>

TEST(ProfileSimulation, RunManyFrames) {
    // Create runtime
    es_runtime_t *rt = es_runtime_create();
    ASSERT_NE(rt, nullptr);

    // Load script - adjust path as needed
    const char* script_path = "../../../assets/main.mr";
    bool loaded = es_runtime_load_script(rt, script_path);
    if (!loaded) {
        // Try alternate path
        loaded = es_runtime_load_script(rt, "assets/main.mr");
    }
    if (!loaded) {
        std::fprintf(stderr, "Warning: Could not load script, skipping profiling test\n");
        es_runtime_destroy(rt);
        GTEST_SKIP();
        return;
    }

    ASSERT_TRUE(es_runtime_has_simulation(rt));

    // Enable ignition and starter
    es_runtime_set_ignition_enabled(rt, true);
    es_runtime_set_starter_enabled(rt, true);
    es_runtime_set_speed_control(rt, 0.5);  // Mid throttle

    // Audio buffer for consuming samples
    int16_t audio_buf[4096];
    int total_steps = 0;
    int total_audio_samples = 0;

    auto start = std::chrono::steady_clock::now();

    // Simulate many frames (10 seconds at 60fps = 600 frames)
    const int num_frames = 600;
    const double dt = 1.0 / 60.0;

    for (int frame = 0; frame < num_frames; ++frame) {
        es_runtime_start_frame(rt, dt);

        int steps = 0;
        while (es_runtime_simulate_step(rt)) {
            ++steps;
        }
        total_steps += steps;

        es_runtime_end_frame(rt);

        // Consume audio like Godot would
        int samples = es_runtime_read_audio(rt, 4096, audio_buf);
        total_audio_samples += samples;

        // Disable starter after a bit
        if (frame == 120) {
            es_runtime_set_starter_enabled(rt, false);
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::fprintf(stderr, "=== Profiling Results ===\n");
    std::fprintf(stderr, "Frames: %d\n", num_frames);
    std::fprintf(stderr, "Total steps: %d\n", total_steps);
    std::fprintf(stderr, "Avg steps/frame: %.1f\n", (double)total_steps / num_frames);
    std::fprintf(stderr, "Total audio samples: %d\n", total_audio_samples);
    std::fprintf(stderr, "Duration: %lld ms\n", duration_ms);
    std::fprintf(stderr, "Simulated time: %.1f s\n", num_frames * dt);
    std::fprintf(stderr, "Real-time ratio: %.2fx\n", (num_frames * dt * 1000.0) / duration_ms);
    std::fprintf(stderr, "=========================\n");

    // Should run faster than real-time for smooth audio
    EXPECT_GT((num_frames * dt * 1000.0) / duration_ms, 1.0) 
        << "Simulation is slower than real-time!";

    es_runtime_destroy(rt);
}

// Quick micro-benchmark of just the physics step
TEST(ProfileSimulation, PhysicsStepMicrobench) {
    es_runtime_t *rt = es_runtime_create();
    ASSERT_NE(rt, nullptr);

    const char* script_path = "../../../assets/main.mr";
    bool loaded = es_runtime_load_script(rt, script_path);
    if (!loaded) {
        loaded = es_runtime_load_script(rt, "assets/main.mr");
    }
    if (!loaded) {
        es_runtime_destroy(rt);
        GTEST_SKIP();
        return;
    }

    es_runtime_set_ignition_enabled(rt, true);
    es_runtime_set_starter_enabled(rt, true);

    // Warm up
    for (int i = 0; i < 60; ++i) {
        es_runtime_start_frame(rt, 1.0/60.0);
        while (es_runtime_simulate_step(rt)) {}
        es_runtime_end_frame(rt);
    }

    es_runtime_set_starter_enabled(rt, false);

    // Time 1000 frames worth of steps
    auto start = std::chrono::steady_clock::now();
    int total_steps = 0;
    for (int i = 0; i < 1000; ++i) {
        es_runtime_start_frame(rt, 1.0/60.0);
        while (es_runtime_simulate_step(rt)) {
            ++total_steps;
        }
        es_runtime_end_frame(rt);
    }
    auto end = std::chrono::steady_clock::now();

    auto us_per_step = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / (double)total_steps;
    std::fprintf(stderr, "=== Physics Microbench ===\n");
    std::fprintf(stderr, "Total steps: %d\n", total_steps);
    std::fprintf(stderr, "Time per step: %.2f us\n", us_per_step);
    std::fprintf(stderr, "Max steps/sec: %.0f\n", 1000000.0 / us_per_step);
    std::fprintf(stderr, "==========================\n");

    es_runtime_destroy(rt);
}
