#include "../include/simulator.h"
#include "../dependencies/submodules/simple-2d-constraint-solver/include/gauss_seidel_sle_solver.h"
#include "../dependencies/submodules/simple-2d-constraint-solver/include/cholesky_sle_solver.h"

#ifndef ENGINE_SIM_ENABLE_SIGNPOST
#define ENGINE_SIM_ENABLE_SIGNPOST 0
#endif

#if ENGINE_SIM_ENABLE_SIGNPOST && defined(__APPLE__)
#include <os/signpost.h>
static os_log_t s_engineSimPerfLog = os_log_create("engine-sim", "perf");
#endif

Simulator::Simulator() {
    m_engine = nullptr;
    m_vehicle = nullptr;
    m_transmission = nullptr;
    m_system = nullptr;

    m_physicsProcessingTime = 0;

    m_simulationSpeed = 1.0;
    m_targetSynthesizerLatency = 0.1;
    m_simulationFrequency = 10000;
    m_steps = 0;

    m_currentIteration = 0;

    m_filteredEngineSpeed = 0.0;
    m_dynoTorqueSamples = nullptr;
    m_lastDynoTorqueSample = 0;
}

Simulator::~Simulator() {
    assert(m_system == nullptr);
    assert(m_dynoTorqueSamples == nullptr);
}

void Simulator::initialize(const Parameters &params) {
    if (params.systemType == SystemType::NsvOptimized) {
        atg_scs::OptimizedNsvRigidBodySystem *system =
            new atg_scs::OptimizedNsvRigidBodySystem;
        atg_scs::GaussSeidelSleSolver *solver = new atg_scs::GaussSeidelSleSolver;
        solver->m_maxIterations = 32;
        solver->m_minDelta = 0.1;
        system->initialize(solver);
        m_system = system;
    }
    else {
        atg_scs::GenericRigidBodySystem *system =
            new atg_scs::GenericRigidBodySystem;
        system->initialize(
            new atg_scs::CholeskySleSolver,
            new atg_scs::NsvOdeSolver);
        m_system = system;
    }

    m_dynoTorqueSamples = new double[DynoTorqueSamples];
    for (int i = 0; i < DynoTorqueSamples; ++i) {
        m_dynoTorqueSamples[i] = 0.0;
    }
}

void Simulator::loadSimulation(Engine *engine, Vehicle *vehicle, Transmission *transmission) {
    m_engine = engine;
    m_vehicle = vehicle;
    m_transmission = transmission;
}

void Simulator::releaseSimulation() {
    destroy();
}

void Simulator::startFrame(double dt) {
    if (m_engine == nullptr) {
        m_steps = 0;
        return;
    }

    m_simulationStart = std::chrono::steady_clock::now();
    m_currentIteration = 0;
    m_synthesizer.setInputSampleRate(m_simulationFrequency * m_simulationSpeed);

    const double timestep = getTimestep();
    m_steps = (int)std::round((dt * m_simulationSpeed) / timestep);

    const double targetLatency = getSynthesizerInputLatencyTarget();
    if (m_synthesizer.getLatency() < targetLatency) {
        m_steps = static_cast<int>((m_steps + 1) * 1.1);
    }
    else if (m_synthesizer.getLatency() > targetLatency) {
        m_steps = static_cast<int>((m_steps - 1) * 0.9);
        if (m_steps < 0) {
            m_steps = 0;
        }
    }

    if (m_steps > 0) {
        for (int i = 0; i < m_engine->getIntakeCount(); ++i) {
            m_engine->getIntake(i)->m_flowRate = 0;
        }
    }
}

// Timing for performance monitoring (simplified)
#ifndef ENGINE_SIM_ENABLE_STEP_TIMING
#define ENGINE_SIM_ENABLE_STEP_TIMING 1  // Enabled for profiling
#endif

#if ENGINE_SIM_ENABLE_STEP_TIMING
static long long s_totalStepTimeNs = 0;
static long long s_physicsTimeNs = 0;
static long long s_updateTimeNs = 0;
static long long s_simStepTimeNs = 0;
static long long s_synthTimeNs = 0;
static int s_profileSteps = 0;
#endif

bool Simulator::simulateStep() {
    if (getCurrentIteration() >= simulationSteps()) {
        auto s1 = std::chrono::steady_clock::now();

        const long long lastFrame =
            std::chrono::duration_cast<std::chrono::microseconds>(s1 - m_simulationStart).count();
        m_physicsProcessingTime = m_physicsProcessingTime * 0.98 + 0.02 * lastFrame;

        #if ENGINE_SIM_ENABLE_STEP_TIMING
        // Print detailed timing every 20000 steps
        if (s_profileSteps >= 20000) {
            const double usPerStep = (s_totalStepTimeNs / 1000.0) / s_profileSteps;
            const double physicsUs = (s_physicsTimeNs / 1000.0) / s_profileSteps;
            const double updateUs = (s_updateTimeNs / 1000.0) / s_profileSteps;
            const double simStepUs = (s_simStepTimeNs / 1000.0) / s_profileSteps;
            const double synthUs = (s_synthTimeNs / 1000.0) / s_profileSteps;
            const double maxStepsPerSec = 1000000.0 / usPerStep;
            std::fprintf(stderr, "engine-sim[perf]: %.1fus/step (max %.0f/s) | physics=%.1fus update=%.1fus simStep=%.1fus synth=%.1fus\n", 
                usPerStep, maxStepsPerSec, physicsUs, updateUs, simStepUs, synthUs);
            s_totalStepTimeNs = 0;
            s_physicsTimeNs = 0;
            s_updateTimeNs = 0;
            s_simStepTimeNs = 0;
            s_synthTimeNs = 0;
            s_profileSteps = 0;
        }
        #endif

        return false;
    }

    #if ENGINE_SIM_ENABLE_SIGNPOST && defined(__APPLE__)
    const os_signpost_id_t sp_step = os_signpost_id_make_with_pointer(s_engineSimPerfLog, this);
    const os_signpost_id_t sp_process = os_signpost_id_generate(s_engineSimPerfLog);
    const os_signpost_id_t sp_update = os_signpost_id_generate(s_engineSimPerfLog);
    const os_signpost_id_t sp_dyno = os_signpost_id_generate(s_engineSimPerfLog);
    const os_signpost_id_t sp_sim_and_synth = os_signpost_id_generate(s_engineSimPerfLog);

    os_signpost_interval_begin(s_engineSimPerfLog, sp_step, "Simulator::simulateStep");
    #endif

    #if ENGINE_SIM_ENABLE_STEP_TIMING
    const auto t0 = std::chrono::steady_clock::now();
    #endif

    const double timestep = getTimestep();

    #if ENGINE_SIM_ENABLE_SIGNPOST && defined(__APPLE__)
    os_signpost_interval_begin(s_engineSimPerfLog, sp_process, "physics::process");
    #endif
    m_system->process(timestep, 1);
    #if ENGINE_SIM_ENABLE_SIGNPOST && defined(__APPLE__)
    os_signpost_interval_end(s_engineSimPerfLog, sp_process, "physics::process");
    #endif

    #if ENGINE_SIM_ENABLE_STEP_TIMING
    const auto t1_physics = std::chrono::steady_clock::now();
    s_physicsTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t1_physics - t0).count();
    #endif

    #if ENGINE_SIM_ENABLE_SIGNPOST && defined(__APPLE__)
    os_signpost_interval_begin(s_engineSimPerfLog, sp_update, "entities::update");
    #endif
    m_engine->update(timestep);
    m_vehicle->update(timestep);
    m_transmission->update(timestep);

    updateFilteredEngineSpeed(timestep);

    #if ENGINE_SIM_ENABLE_SIGNPOST && defined(__APPLE__)
    os_signpost_interval_end(s_engineSimPerfLog, sp_update, "entities::update");
    os_signpost_interval_begin(s_engineSimPerfLog, sp_dyno, "dyno::sample");
    #endif

    #if ENGINE_SIM_ENABLE_STEP_TIMING
    const auto t2_update = std::chrono::steady_clock::now();
    s_updateTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t2_update - t1_physics).count();
    #endif

    Crankshaft *outputShaft = m_engine->getOutputCrankshaft();
    outputShaft->resetAngle();

    for (int i = 0; i < m_engine->getCrankshaftCount(); ++i) {
        Crankshaft *shaft = m_engine->getCrankshaft(i);
        shaft->m_body.theta = outputShaft->m_body.theta;
    }

    const double cycleAngle = outputShaft->getCycleAngle();
    const int index = static_cast<int>(std::floor(
        DynoTorqueSamples * cycleAngle / (4 * constants::pi)
    ));
    const int step = m_engine->isSpinningCw() ? 1 : -1;
    const double torque = m_dyno.getTorque();
    m_dynoTorqueSamples[index] = torque;

    if (m_lastDynoTorqueSample != index) {
        for (int i = m_lastDynoTorqueSample + step; i != index; i += step) {
            if (i >= DynoTorqueSamples) {
                i = -1;
                continue;
            }
            else if (i < 0) {
                i = DynoTorqueSamples;
                continue;
            }

            m_dynoTorqueSamples[i] = torque;
        }

        m_lastDynoTorqueSample = index;
    }

    #if ENGINE_SIM_ENABLE_SIGNPOST && defined(__APPLE__)
    os_signpost_interval_end(s_engineSimPerfLog, sp_dyno, "dyno::sample");
    os_signpost_interval_begin(s_engineSimPerfLog, sp_sim_and_synth, "simulateStep_+synth");
    #endif

    #if ENGINE_SIM_ENABLE_STEP_TIMING
    const auto t3_dyno = std::chrono::steady_clock::now();
    #endif

    simulateStep_();

    #if ENGINE_SIM_ENABLE_STEP_TIMING
    const auto t4_simstep = std::chrono::steady_clock::now();
    s_simStepTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t4_simstep - t3_dyno).count();
    #endif

    writeToSynthesizer();

    #if ENGINE_SIM_ENABLE_STEP_TIMING
    const auto t5_synth = std::chrono::steady_clock::now();
    s_synthTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t5_synth - t4_simstep).count();
    #endif

    #if ENGINE_SIM_ENABLE_SIGNPOST && defined(__APPLE__)
    os_signpost_interval_end(s_engineSimPerfLog, sp_sim_and_synth, "simulateStep_+synth");
    os_signpost_interval_end(s_engineSimPerfLog, sp_step, "Simulator::simulateStep");
    #endif

    #if ENGINE_SIM_ENABLE_STEP_TIMING
    const auto t1 = std::chrono::steady_clock::now();
    s_totalStepTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    ++s_profileSteps;
    #endif

    ++m_currentIteration;
    return true;
}

double Simulator::getTotalExhaustFlow() const {
    return 0.0;
}

int Simulator::readAudioOutput(int samples, int16_t *target) {
    return m_synthesizer.readAudioOutput(samples, target);
}

void Simulator::endFrame() {
    m_synthesizer.endInputBlock();
}

void Simulator::destroy() {
    m_synthesizer.endAudioRenderingThread();
    m_synthesizer.destroy();

    if (m_system != nullptr) {
        m_system->reset();
        delete m_system;
        m_system = nullptr;
    }

    if (m_dynoTorqueSamples != nullptr) {
        delete[] m_dynoTorqueSamples;
        m_dynoTorqueSamples = nullptr;
    }
}

void Simulator::startAudioRenderingThread() {
    m_synthesizer.startAudioRenderingThread();
}

void Simulator::endAudioRenderingThread() {
    m_synthesizer.endAudioRenderingThread();
}

double Simulator::getSynthesizerInputLatencyTarget() const {
    return m_targetSynthesizerLatency;
}

double Simulator::getFilteredDynoTorque() const {
    if (m_dynoTorqueSamples == nullptr) return 0;

    double averageTorque = 0;
    for (int i = 0; i < DynoTorqueSamples; ++i) {
        averageTorque += m_dynoTorqueSamples[i];
    }

    return averageTorque / DynoTorqueSamples;
}

double Simulator::getDynoPower() const {
    return (m_engine != nullptr)
        ? getFilteredDynoTorque() * m_engine->getSpeed()
        : 0;
}

double Simulator::getAverageOutputSignal() const {
    return 0.0;
}

void Simulator::initializeSynthesizer() {
    Synthesizer::Parameters synthParams;
    // Use 44100 Hz to match Godot's default mix rate
    synthParams.audioBufferSize = 44100 * 2;  // 2 second buffer
    synthParams.audioSampleRate = 44100;
    synthParams.inputBufferSize = 44100;
    synthParams.inputChannelCount = m_engine->getExhaustSystemCount();
    synthParams.inputSampleRate = static_cast<float>(getSimulationFrequency());
    
    std::fprintf(stderr, "engine-sim: initializeSynthesizer: simFreq=%d inputSampleRate=%.1f audioSampleRate=%.1f channels=%d\n",
        getSimulationFrequency(), synthParams.inputSampleRate, synthParams.audioSampleRate, synthParams.inputChannelCount);
    
    m_synthesizer.initialize(synthParams);
}

void Simulator::simulateStep_() {
}

void Simulator::updateFilteredEngineSpeed(double dt) {
    const double alpha = dt / (100 + dt);
    m_filteredEngineSpeed = alpha * m_filteredEngineSpeed + (1 - alpha) * m_engine->getRpm();
}
