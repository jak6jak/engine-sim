#include "register_types.h"

#include "engine_sim_runtime_node.h"

#include <godot_cpp/core/class_db.hpp>

namespace godot {

void initialize_engine_sim(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    ClassDB::register_class<EngineSimRuntime>();
}

void uninitialize_engine_sim(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

} // namespace godot
