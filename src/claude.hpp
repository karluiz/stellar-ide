#pragma once

#include "workspace.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace raven {

enum class Harness {
    ClaudeCode,
    KimiCode,
};

enum class EventKind {
    SessionStarted,
    ThoughtDelta,
    AssistantDelta,
    AssistantMessage,
    ToolStarted,
    ToolFinished,
    SubagentStarted,
    SubagentFinished,
    RunFinished,
    Error,
};

struct ClaudeEvent {
    EventKind kind = EventKind::Error;
    std::string session_id;
    std::string message_id;
    std::string parent_tool_id;
    std::string tool_id;
    std::string name;
    std::string text;
    std::string detail;
    bool is_error = false;
    double cost_usd = -1.0;
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
    bool has_usage = false;
};

class ClaudeEventParser {
public:
    std::vector<ClaudeEvent> parse(const std::string& line) const;
};

class KimiEventParser {
public:
    std::vector<ClaudeEvent> parse(const std::string& line) const;
};

class KimiAcpEventParser {
public:
    std::vector<ClaudeEvent> parse(const std::string& line) const;
};

struct RunOptions {
    std::filesystem::path working_directory;
    std::string prompt;
    std::string session_id;
    std::string agent;
    std::string model;
    std::string effort;
    std::filesystem::path agent_file;
    std::filesystem::path knowledge_binary;
    std::vector<std::filesystem::path> resource_links;
    Harness harness = Harness::ClaudeCode;
    bool read_only = false;
    bool wiki_schema = false;
};

class ClaudeProcess {
public:
    ClaudeProcess();
    ~ClaudeProcess();

    ClaudeProcess(const ClaudeProcess&) = delete;
    ClaudeProcess& operator=(const ClaudeProcess&) = delete;

    bool start(const RunOptions& options, std::string& error);
    void stop();
    bool running() const;
    std::deque<ClaudeEvent> drain();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool run_kimi_acp(int output_fd, int input_fd, const RunOptions& options);
};

std::string wiki_schema();
std::string claude_agents_json(const std::vector<AgentDefinition>& agents);

} // namespace raven
