#include "verification.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <regex>
#include <sstream>
#include <sys/wait.h>

namespace raven {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int number(const std::sub_match<const char*>& match) {
    try {
        return std::stoi(match.str());
    } catch (const std::exception&) {
        return 0;
    }
}

std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (const char c : value) {
        if (c == '\'') result += "'\\''";
        else result.push_back(c);
    }
    return result + "'";
}

} // namespace

VerificationCheck run_verification_command(const std::string& id, const std::string& command, const std::filesystem::path& working_directory) {
    VerificationCheck result{id, command, verification_command_kind(command), VerificationStatus::Failed, {}};
    if (result.kind.empty()) result.kind = "check";
    const auto invocation = "cd " + shell_quote(working_directory.string()) + " && " + command;
    FILE* pipe = popen((invocation + " 2>&1").c_str(), "r");
    if (!pipe) {
        result.output = "Could not start command";
        return result;
    }
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) result.output += buffer.data();
    const auto status = pclose(pipe);
    result.status = status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? VerificationStatus::Passed : VerificationStatus::Failed;
    if (result.output.size() > 8'000) result.output = result.output.substr(0, 4'000) + "\n…\n" + result.output.substr(result.output.size() - 4'000);
    return result;
}

std::string verification_command_kind(const std::string& command) {
    const auto value = lowercase(command);
    static const std::regex tests(
        R"((^|[\s;&|])(ctest|pytest|py\.test|cargo\s+test|go\s+test|npm\s+(run\s+)?test|pnpm\s+(run\s+)?test|yarn\s+test|dotnet\s+test|mvn\s+test|gradle\s+test)(?=\s|$))",
        std::regex::icase);
    if (std::regex_search(value, tests)) return "test";
    if (value.find("cmake --build") != std::string::npos) return "build";
    static const std::regex builds(
        R"((^|[\s;&|])(cmake\s+--build|ninja|make|g\+\+|clang\+\+|cargo\s+build|go\s+build|npm\s+run\s+build|pnpm\s+run\s+build|yarn\s+build|tsc|dotnet\s+build|mvn\s+package)(?=\s|$))",
        std::regex::icase);
    if (std::regex_search(value, builds)) return "build";
    return {};
}

const char* verification_status_name(VerificationStatus status) {
    switch (status) {
    case VerificationStatus::Running: return "running";
    case VerificationStatus::Passed: return "passed";
    case VerificationStatus::Failed: return "failed";
    }
    return "failed";
}

std::vector<Diagnostic> parse_diagnostics(const std::string& output) {
    std::vector<Diagnostic> result;
    static const std::regex native(
        R"(^(.+?):([0-9]+)(?::([0-9]+))?:\s+(fatal error|error|warning|note):\s*(.*)$)",
        std::regex::icase);
    static const std::regex python(R"re(^\s*File "([^"]+)", line ([0-9]+)(?:, in (.*))?$)re");
    static const std::regex rust(R"(^\s*-->\s+(.+):([0-9]+):([0-9]+)\s*$)");
    std::istringstream input(output);
    std::string line;
    while (std::getline(input, line) && result.size() < 200) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::match_results<const char*> match;
        const char* text = line.c_str();
        if (std::regex_match(text, match, native)) {
            result.push_back({match[1].str(), number(match[2]), number(match[3]),
                lowercase(match[4].str()), match[5].str()});
        } else if (std::regex_match(text, match, python)) {
            result.push_back({match[1].str(), number(match[2]), 0, "error",
                match[3].matched ? "in " + match[3].str() : "Python traceback"});
        } else if (std::regex_match(text, match, rust)) {
            result.push_back({match[1].str(), number(match[2]), number(match[3]), "error", "Rust diagnostic"});
        }
    }
    return result;
}

} // namespace raven
