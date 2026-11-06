#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace raven {

enum class VerificationStatus {
    Running,
    Passed,
    Failed,
};

struct VerificationCheck {
    std::string id;
    std::string command;
    std::string kind;
    VerificationStatus status = VerificationStatus::Running;
    std::string output;
};

struct Diagnostic {
    std::string path;
    int line = 0;
    int column = 0;
    std::string severity;
    std::string message;
};

std::string verification_command_kind(const std::string& command);
const char* verification_status_name(VerificationStatus status);
std::vector<Diagnostic> parse_diagnostics(const std::string& output);
VerificationCheck run_verification_command(const std::string& id, const std::string& command, const std::filesystem::path& working_directory);

} // namespace raven
