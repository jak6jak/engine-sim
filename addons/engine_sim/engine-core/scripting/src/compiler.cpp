#include "../include/compiler.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

es_script::Compiler::Output *es_script::Compiler::s_output = nullptr;

es_script::Compiler::Compiler() {
    m_compiler = nullptr;
}

es_script::Compiler::~Compiler() {
    assert(m_compiler == nullptr);
}

es_script::Compiler::Output *es_script::Compiler::output() {
    if (s_output == nullptr) {
        s_output = new Output;
    }

    return s_output;
}

void es_script::Compiler::initialize() {
    m_compiler = new piranha::Compiler(&m_rules);
    m_compiler->setFileExtension(".mr");
    m_rules.initialize();
}

void es_script::Compiler::addSearchPath(const std::string &path) {
    if (m_compiler != nullptr) {
        m_compiler->addSearchPath(path);
    }
}

void es_script::Compiler::addSearchPathOnce(const std::string &path) {
    if (path.empty()) {
        return;
    }

    for (const std::string &existing : m_dynamicSearchPaths) {
        if (existing == path) {
            return;
        }
    }

    m_dynamicSearchPaths.push_back(path);
    addSearchPath(path);
}

bool es_script::Compiler::compile(const piranha::IrPath &path) {
    bool successful = false;

    // Create a fresh compiler per compile so search path priority is deterministic.
    // Piranha resolves imports by checking search paths in insertion order.
    // We want the script's directory tree to win over engine-core/es fallbacks.
    if (m_compiler != nullptr) {
        m_compiler->free();
        delete m_compiler;
        m_compiler = nullptr;
    }

    m_dynamicSearchPaths.clear();
    m_compiler = new piranha::Compiler(&m_rules);
    m_compiler->setFileExtension(".mr");

    // Highest priority: script directory and its ancestors.
    try {
        std::filesystem::path script_path(path.toString());
        std::filesystem::path dir = script_path.parent_path();
        for (int i = 0; i < 6 && !dir.empty(); ++i) {
            std::string dir_str = dir.string();
            if (!dir_str.empty() && dir_str.back() != '/') {
                dir_str.push_back('/');
            }
            addSearchPathOnce(dir_str);
            dir = dir.parent_path();
        }
    }
    catch (...) {
        // Best effort only.
    }

    // Lower priority: cwd-relative fallbacks for legacy layouts.
    addSearchPath("./");
    addSearchPath("../");
    addSearchPath("../../");
    addSearchPath("../../../");
    addSearchPath("../../es/");
    addSearchPath("../es/");
    addSearchPath("es/");

    std::ostringstream log;
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
        log << "engine-sim script compile log: "
            << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S")
            << "\n";
    }

    piranha::IrCompilationUnit *unit = m_compiler->compile(path);
    if (unit == nullptr) {
        log << "Can't find file: " << path.toString() << "\n";
    }
    else {
        const piranha::ErrorList *errors = m_compiler->getErrorList();
        if (errors->getErrorCount() == 0) {
            // Some compilation errors (e.g. missing ports/types) can be discovered while
            // building the runtime node graph. Ensure we capture those too.
            unit->build(&m_program);

            errors = m_compiler->getErrorList();
            if (errors->getErrorCount() == 0) {
                m_program.initialize();
                successful = true;
                log << "OK\n";
            }
            else {
                for (int i = 0; i < errors->getErrorCount(); ++i) {
                    printError(errors->getCompilationError(i), log);
                }
            }
        }
        else {
            for (int i = 0; i < errors->getErrorCount(); ++i) {
                printError(errors->getCompilationError(i), log);
            }
        }
    }

    // Write logs in two places:
    // 1) Current working directory (legacy behavior): ./error_log.log
    // 2) Next to the script being compiled (more discoverable): <script_dir>/error_log.log
    {
        const std::string log_text = log.str();

        // Legacy log location
        {
            std::ofstream file("error_log.log", std::ios::out);
            file << log_text;
        }

        // Script-adjacent log location
        try {
            const std::filesystem::path script_path(path.toString());
            if (!script_path.empty()) {
                const std::filesystem::path script_dir = script_path.parent_path();
                if (!script_dir.empty()) {
                    const std::filesystem::path log_path = script_dir / "error_log.log";
                    std::ofstream file(log_path, std::ios::out);
                    file << log_text;
                }
            }
        }
        catch (...) {
            // Best effort only; legacy log still exists.
        }
    }

    return successful;
}

es_script::Compiler::Output es_script::Compiler::execute() {
    const bool result = m_program.execute();

    if (!result) {
        // Todo: Runtime error
    }

    return *output();
}

void es_script::Compiler::destroy() {
    m_program.free();
    m_compiler->free();

    delete m_compiler;
    m_compiler = nullptr;
}

void es_script::Compiler::printError(
    const piranha::CompilationError *err,
    std::ostream &out) const
{
    const piranha::ErrorCode_struct &errorCode = err->getErrorCode();
    out << err->getCompilationUnit()->getPath().getStem()
        << "(" << err->getErrorLocation()->lineStart << "): error "
        << errorCode.stage << errorCode.code << ": " << errorCode.info << std::endl;

    piranha::IrContextTree *context = err->getInstantiation();
    while (context != nullptr) {
        piranha::IrNode *instance = context->getContext();
        if (instance != nullptr) {
            const std::string instanceName = instance->getName();
            const std::string definitionName = (instance->getDefinition() != nullptr)
                ? instance->getDefinition()->getName()
                : "<Type Error>";
            const std::string formattedName = (instanceName.empty())
                ? "<unnamed> " + definitionName
                : instanceName + " " + definitionName;

            out
                << "       While instantiating: "
                << instance->getParentUnit()->getPath().getStem()
                << "(" << instance->getSummaryToken()->lineStart << "): "
                << formattedName << std::endl;
        }

        context = context->getParent();
    }
}
