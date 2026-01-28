cmake_minimum_required(VERSION 3.10...3.27)

# Allow older third-party projects to configure under newer CMake.
if (NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

# Prefer new behavior for URL extraction timestamps when available.
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

option(DTV "Enable video output" OFF)
option(PIRANHA_ENABLED "Enable scripting input" ON)
option(DISCORD_ENABLED "Enable Discord Rich Presence" ON)

# Perf / profiling toggles (compile-time)
option(ENGINE_SIM_ENABLE_STEP_TIMING "Enable simple per-step timing prints" OFF)
option(ENGINE_SIM_ENABLE_SIGNPOST "Enable macOS Instruments signposts (Points of Interest)" OFF)

if (DTV)
    add_compile_definitions(ATG_ENGINE_SIM_VIDEO_CAPTURE)
endif (DTV)

if (PIRANHA_ENABLED)
    add_compile_definitions(ATG_ENGINE_SIM_PIRANHA_ENABLED)
endif (PIRANHA_ENABLED)

if (DISCORD_ENABLED)
    add_compile_definitions(ATG_ENGINE_SIM_DISCORD_ENABLED)
endif (DISCORD_ENABLED)

# Enable group projects in folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

project(engine-sim)

set(CMAKE_CXX_STANDARD 17)

# Define __forceinline for non-MSVC compilers
if(NOT MSVC)
    add_compile_definitions(__forceinline=inline)
endif()

# ========================================================
# macOS toolchain helpers
#
# macOS ships an old bison (2.3) that is too old for piranha's grammar.
# Prefer Homebrew flex/bison when available.
if (APPLE)
    find_program(HOMEBREW_BISON bison
        PATHS
            /opt/homebrew/opt/bison/bin
            /usr/local/opt/bison/bin
            /opt/homebrew/bin
            /usr/local/bin
        NO_DEFAULT_PATH)
    if (HOMEBREW_BISON)
        set(BISON_EXECUTABLE ${HOMEBREW_BISON} CACHE FILEPATH "Bison executable" FORCE)
    endif()

    find_program(HOMEBREW_FLEX flex
        PATHS
            /opt/homebrew/opt/flex/bin
            /usr/local/opt/flex/bin
            /opt/homebrew/bin
            /usr/local/bin
        NO_DEFAULT_PATH)
    if (HOMEBREW_FLEX)
        set(FLEX_EXECUTABLE ${HOMEBREW_FLEX} CACHE FILEPATH "Flex executable" FORCE)
    endif()
endif()

# ========================================================
# GTEST

include(FetchContent)
FetchContent_Declare(
    googletest
    URL
    https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set_property(TARGET gmock PROPERTY FOLDER "gtest")
set_property(TARGET gmock_main PROPERTY FOLDER "gtest")
set_property(TARGET gtest PROPERTY FOLDER "gtest")
set_property(TARGET gtest_main PROPERTY FOLDER "gtest")

# ========================================================

add_library(engine-sim STATIC
    # Source files
    src/audio_buffer.cpp
    src/camshaft.cpp
    src/crankshaft.cpp
    src/combustion_chamber.cpp
    src/connecting_rod.cpp
    src/convolution_filter.cpp
    src/cylinder_bank.cpp
    src/cylinder_head.cpp
    src/delay_filter.cpp
    src/derivative_filter.cpp
    src/direct_throttle_linkage.cpp
    src/dynamometer.cpp
    src/engine.cpp
    src/exhaust_system.cpp
    src/feedback_comb_filter.cpp
    src/filter.cpp
    src/fuel.cpp
    src/function.cpp
    src/gas_system.cpp
    src/gaussian_filter.cpp
    src/governor.cpp
    src/ignition_module.cpp
    src/impulse_response.cpp
    src/intake.cpp
    src/jitter_filter.cpp
    src/leveling_filter.cpp
    src/low_pass_filter.cpp
    src/part.cpp
    src/piston.cpp
    src/piston_engine_simulator.cpp
    src/simulator.cpp
    src/standard_valvetrain.cpp
    src/starter_motor.cpp
    src/synthesizer.cpp
    src/throttle.cpp
    src/transmission.cpp
    src/utilities.cpp
    src/valvetrain.cpp
    src/vehicle.cpp
    src/vehicle_drag_constraint.cpp
    src/vtec_valvetrain.cpp

    # Include files
    include/audio_buffer.h
    include/application_settings.h
    include/camshaft.h
    include/crankshaft.h
    include/combustion_chamber.h
    include/connecting_rod.h
    include/convolution_filter.h
    include/cylinder_bank.h
    include/cylinder_head.h
    include/delay_filter.h
    include/derivative_filter.h
    include/direct_throttle_linkage.h
    include/dynamometer.h
    include/engine.h
    include/exhaust_system.h
    include/feedback_comb_filter.h
    include/filter.h
    include/fuel.h
    include/function.h
    include/gas_system.h
    include/gaussian_filter.h
    include/governor.h
    include/ignition_module.h
    include/impulse_response.h
    include/intake.h
    include/jitter_filter.h
    include/leveling_filter.h
    include/low_pass_filter.h
    include/part.h
    include/piston.h
    include/piston_engine_simulator.h
    include/simulator.h
    include/standard_valvetrain.h
    include/starter_motor.h
    include/synthesizer.h
    include/throttle.h
    include/transmission.h
    include/units.h
    include/utilities.h
    include/valvetrain.h
    include/vehicle.h
    include/vehicle_drag_constraint.h
    include/vtec_valvetrain.h
)

set_target_properties(engine-sim PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (ENGINE_SIM_ENABLE_STEP_TIMING)
    target_compile_definitions(engine-sim PRIVATE ENGINE_SIM_ENABLE_STEP_TIMING=1)
endif()

if (ENGINE_SIM_ENABLE_SIGNPOST)
    target_compile_definitions(engine-sim PRIVATE ENGINE_SIM_ENABLE_SIGNPOST=1)
endif()

target_link_libraries(engine-sim
    simple-2d-constraint-solver)

target_include_directories(engine-sim
    PUBLIC dependencies/submodules)

add_library(engine-sim-runtime STATIC
    src/engine_sim_runtime_c.cpp
    include/engine_sim_runtime_c.h
)

set_target_properties(engine-sim-runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(engine-sim-runtime
    engine-sim)

target_include_directories(engine-sim-runtime
    PUBLIC
        include
        dependencies/submodules)

if (PIRANHA_ENABLED)
    add_library(engine-sim-script-interpreter STATIC
        # Source files
        scripting/src/channel_types.cpp
        scripting/src/compiler.cpp
        scripting/src/engine_context.cpp
        scripting/src/language_rules.cpp

        # Include files
        scripting/include/actions.h
        scripting/include/camshaft_node.h
        scripting/include/channel_types.h
        scripting/include/compiler.h
        scripting/include/connecting_rod_node.h
        scripting/include/crankshaft_node.h
        scripting/include/cylinder_bank_node.h
        scripting/include/cylinder_head_node.h
        scripting/include/engine_context.h
        scripting/include/engine_node.h
        scripting/include/engine_sim.h
        scripting/include/exhaust_system_node.h
        scripting/include/intake_node.h
        scripting/include/function_node.h
        scripting/include/ignition_module_node.h
        scripting/include/ignition_wire_node.h
        scripting/include/impulse_response_node.h
        scripting/include/intake_node.h
        scripting/include/language_rules.h
        scripting/include/node.h
        scripting/include/object_reference_node.h
        scripting/include/object_reference_node_output.h
        scripting/include/piranha.h
        scripting/include/piston_node.h
        scripting/include/rod_journal_node.h
        scripting/include/standard_valvetrain_node.h
        scripting/include/transmission_node.h
        scripting/include/valvetrain_node.h
        scripting/include/vtec_valvetrain_node.h
		scripting/include/vehicle_node.h
    )

    target_include_directories(engine-sim-script-interpreter
        PUBLIC dependencies/submodules)

    target_link_libraries(engine-sim-script-interpreter
        piranha)

    target_link_libraries(engine-sim-runtime
        engine-sim-script-interpreter)
endif (PIRANHA_ENABLED)

if (DISCORD_ENABLED)
    add_library(discord STATIC
        # Source files
        dependencies/discord/Discord.cpp

        # Include files
        dependencies/discord/Discord.h
        dependencies/discord/discord_register.h
        dependencies/discord/discord_rpc.h
    )
endif (DISCORD_ENABLED)


add_subdirectory(dependencies)

# GTEST

enable_testing()

add_executable(engine-sim-test
    # Source files
    test/gas_system_tests.cpp
    test/function_test.cpp
    test/script_compile_tests.cpp
    test/synthesizer_tests.cpp
    test/profile_sim.cpp
)

target_link_libraries(engine-sim-test
    gtest_main
    engine-sim
    engine-sim-runtime
)

include(GoogleTest)
gtest_discover_tests(engine-sim-test)
