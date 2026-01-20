#include <gtest/gtest.h>

#include "../include/engine_sim_runtime_c.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string read_text_file(const std::filesystem::path &path) {
    std::ifstream file(path);
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::filesystem::path find_project_root_from_this_file() {
    // __FILE__ points to: .../addons/engine_sim/engine-core/test/<this_file>
    const std::filesystem::path test_dir = std::filesystem::path(__FILE__).parent_path();
    const std::filesystem::path engine_core_dir = test_dir.parent_path();

    // engine-core -> engine_sim -> addons -> <project_root>
    return engine_core_dir.parent_path().parent_path().parent_path();
}

} // namespace

TEST(ScriptCompileTests, CreatesFreshErrorLogOnCompile) {
#if !defined(ATG_ENGINE_SIM_PIRANHA_ENABLED)
    GTEST_SKIP() << "Scripting disabled (ATG_ENGINE_SIM_PIRANHA_ENABLED not set).";
#else
    namespace fs = std::filesystem;

    const fs::path project_root = find_project_root_from_this_file();
    const fs::path script_path = project_root / "assets" / "main.mr";

    ASSERT_TRUE(fs::exists(script_path)) << "Expected script not found: " << script_path.string();

    const fs::path cwd_log = fs::current_path() / "error_log.log";
    const fs::path script_log = script_path.parent_path() / "error_log.log";

    std::error_code ec;
    fs::remove(cwd_log, ec);
    fs::remove(script_log, ec);

    es_runtime_t *rt = es_runtime_create();
    ASSERT_NE(rt, nullptr);

    // We don't assert success here; this test is about log creation/freshness.
    (void)es_runtime_load_script(rt, script_path.string().c_str());

    es_runtime_destroy(rt);

    const bool has_any_log = fs::exists(cwd_log) || fs::exists(script_log);
    ASSERT_TRUE(has_any_log) << "Expected compiler to create error_log.log at cwd and/or script directory";

    if (fs::exists(cwd_log)) {
        const std::string content = read_text_file(cwd_log);
        EXPECT_NE(content.find("engine-sim script compile log:"), std::string::npos);
    }

    if (fs::exists(script_log)) {
        const std::string content = read_text_file(script_log);
        EXPECT_NE(content.find("engine-sim script compile log:"), std::string::npos);
    }
#endif
}

TEST(ScriptRuntimeTests, BusEngineCranksAndKeepsRunningBriefly) {
#if !defined(ATG_ENGINE_SIM_PIRANHA_ENABLED)
    GTEST_SKIP() << "Scripting disabled (ATG_ENGINE_SIM_PIRANHA_ENABLED not set).";
#else
    namespace fs = std::filesystem;

    const fs::path project_root = find_project_root_from_this_file();
    const fs::path script_path = project_root / "assets" / "main.mr";
    ASSERT_TRUE(fs::exists(script_path)) << "Expected script not found: " << script_path.string();

    es_runtime_t *rt = es_runtime_create();
    ASSERT_NE(rt, nullptr);
    ASSERT_TRUE(es_runtime_load_script(rt, script_path.string().c_str()));
    ASSERT_TRUE(es_runtime_has_simulation(rt));

    // Match the Godot demos: start in neutral, clutch disengaged, small speed_control.
    // This avoids loading the engine during cranking.
    es_runtime_set_gear(rt, -1);
    es_runtime_set_clutch_pressure(rt, 0.0);
    es_runtime_set_speed_control(rt, 1.0);
    es_runtime_set_ignition_enabled(rt, true);
    es_runtime_set_starter_enabled(rt, true);

    auto simulate_seconds = [&](double seconds) {
        const double dt = 1.0 / 120.0;
        const int frames = static_cast<int>(seconds / dt);
        for (int i = 0; i < frames; ++i) {
            es_runtime_start_frame(rt, dt);
            while (es_runtime_simulate_step(rt)) {
                // step until frame completes
            }
            es_runtime_end_frame(rt);
        }
    };

    // The bus engine model intentionally uses a very low starter speed (~30 RPM),
    // so it may take multiple seconds to reach a firing event and build momentum.
    // Simulate longer cranking time to allow the slow starter to build RPM.
    simulate_seconds(15.0);
    const double rpm_while_cranking = es_runtime_get_engine_speed_raw(rt);

    es_runtime_set_starter_enabled(rt, false);
    simulate_seconds(3.0);
    const double rpm_after_starter_off = es_runtime_get_engine_speed_raw(rt);

    es_runtime_destroy(rt);

    // If combustion catches, RPM should remain meaningfully above zero after starter is off.
    // Threshold is intentionally low to avoid flakiness across models.
    // For slow-cranking diesel engines like the bus, we just verify it's running.
    EXPECT_GT(rpm_after_starter_off, 50.0)
        << "RPM while cranking=" << rpm_while_cranking
        << ", RPM after starter off=" << rpm_after_starter_off;
#endif
}
