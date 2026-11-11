#include "claude.hpp"

#include <Raven.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace raven {
namespace {

using json = nlohmann::json;

std::string string_at(const json& value, const char* key) {
    return value.is_object() && value.contains(key) && value[key].is_string() ? value[key].get<std::string>() : "";
}

std::string image_mime_type(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".bmp") return "image/bmp";
    if (extension == ".tga") return "image/tga";
    return "image/png";
}

std::uint64_t token_at(const json& value, const char* key) {
    return value.is_object() && value.contains(key) && value[key].is_number_unsigned()
        ? value[key].get<std::uint64_t>() : 0;
}

void apply_usage(ClaudeEvent& event, const json& input) {
    if (!input.is_object()) return;
    if (input.contains("total_cost_usd") && input["total_cost_usd"].is_number()) {
        event.cost_usd = input["total_cost_usd"].get<double>();
        event.has_usage = true;
    }
    if (!input.contains("usage") || !input["usage"].is_object()) return;
    const auto& usage = input["usage"];
    event.input_tokens = token_at(usage, "input_tokens") + token_at(usage, "cache_creation_input_tokens") +
        token_at(usage, "cache_read_input_tokens");
    event.output_tokens = token_at(usage, "output_tokens");
    event.has_usage = true;
}

std::string content_text(const json& content) {
    if (content.is_string()) return content.get<std::string>();
    if (content.is_object() && string_at(content, "type") == "text") return string_at(content, "text");
    std::string result;
    if (!content.is_array()) return result;
    for (const auto& block : content) {
        if (block.is_object() && string_at(block, "type") == "text") {
            if (!result.empty()) result += "\n";
            result += string_at(block, "text");
        }
    }
    return result;
}

ClaudeEvent base_event(const json& input, EventKind kind) {
    ClaudeEvent event;
    event.kind = kind;
    event.session_id = string_at(input, "session_id");
    event.parent_tool_id = string_at(input, "parent_tool_use_id");
    event.message_id = string_at(input, "uuid");
    return event;
}

std::string tool_detail(const json& input) {
    if (!input.is_object()) return {};
    for (const char* key : {"description", "command", "file_path", "path", "pattern", "url", "query"}) {
        if (input.contains(key) && input[key].is_string()) return input[key].get<std::string>();
    }
    return {};
}

std::string trimmed(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string scalar(const std::string& value) {
    const auto result = trimmed(value);
    if (result.size() >= 2 && ((result.front() == '"' && result.back() == '"') ||
        (result.front() == '\'' && result.back() == '\''))) {
        return result.substr(1, result.size() - 2);
    }
    return result;
}

json tool_list(const std::string& value) {
    auto list = trimmed(value);
    if (list.size() >= 2 && list.front() == '[' && list.back() == ']') {
        list = list.substr(1, list.size() - 2);
    }
    json result = json::array();
    std::istringstream input(list);
    std::string tool;
    while (std::getline(input, tool, ',')) {
        auto name = scalar(tool);
        if (!name.empty()) result.push_back(std::move(name));
    }
    return result;
}

json claude_agent_definition(const AgentDefinition& agent) {
    json definition = {{"description", agent.description}, {"prompt", agent.content}};
    std::istringstream input(agent.content);
    std::string line;
    if (!std::getline(input, line) || trimmed(line) != "---") return definition;
    while (std::getline(input, line)) {
        if (trimmed(line) == "---") {
            definition["prompt"] = std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
            break;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto key = trimmed(line.substr(0, colon));
        const auto value = trimmed(line.substr(colon + 1));
        if (key == "description") {
            definition[key] = scalar(value);
        } else if (key == "tools" || key == "disallowedTools") {
            if (!value.empty()) {
                definition[key] = tool_list(value);
                continue;
            }
            json tools = json::array();
            while (input.peek() == ' ' || input.peek() == '\t') {
                std::string item;
                std::getline(input, item);
                item = trimmed(item);
                if (item.rfind("- ", 0) != 0) continue;
                auto name = scalar(item.substr(2));
                if (!name.empty()) tools.push_back(std::move(name));
            }
            definition[key] = std::move(tools);
        }
    }
    return definition;
}

} // namespace

std::string base64_encode(std::string_view data) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        const auto a = static_cast<unsigned char>(data[i]);
        const auto b = i + 1 < data.size() ? static_cast<unsigned char>(data[i + 1]) : 0;
        const auto c = i + 2 < data.size() ? static_cast<unsigned char>(data[i + 2]) : 0;
        result.push_back(alphabet[a >> 2]);
        result.push_back(alphabet[((a & 0x03) << 4) | (b >> 4)]);
        result.push_back(i + 1 < data.size() ? alphabet[((b & 0x0f) << 2) | (c >> 6)] : '=');
        result.push_back(i + 2 < data.size() ? alphabet[c & 0x3f] : '=');
    }
    return result;
}

std::vector<ClaudeEvent> ClaudeEventParser::parse(const std::string& line) const {
    std::vector<ClaudeEvent> events;
    const json input = json::parse(line);
    const auto type = string_at(input, "type");

    if (type == "system" && string_at(input, "subtype") == "init") {
        auto event = base_event(input, EventKind::SessionStarted);
        event.name = string_at(input, "model");
        event.detail = string_at(input, "cwd");
        events.push_back(std::move(event));
    } else if (type == "stream_event" && input.contains("event")) {
        const auto& stream = input["event"];
        const auto subtype = string_at(stream, "type");
        if (subtype == "content_block_delta" && stream.contains("delta") &&
            string_at(stream["delta"], "type") == "text_delta") {
            auto event = base_event(input, EventKind::AssistantDelta);
            event.text = string_at(stream["delta"], "text");
            events.push_back(std::move(event));
        }
    } else if (type == "assistant" && input.contains("message")) {
        const auto& message = input["message"];
        for (const auto& block : message.value("content", json::array())) {
            const auto block_type = string_at(block, "type");
            if (block_type == "text") {
                auto event = base_event(input, EventKind::AssistantMessage);
                event.message_id = string_at(message, "id");
                event.text = string_at(block, "text");
                events.push_back(std::move(event));
            } else if (block_type == "tool_use") {
                const auto name = string_at(block, "name");
                const bool subagent = name == "Task" || name == "Agent";
                auto event = base_event(input, subagent ? EventKind::SubagentStarted : EventKind::ToolStarted);
                event.tool_id = string_at(block, "id");
                event.name = subagent && block.contains("input") ? string_at(block["input"], "subagent_type") : name;
                if (event.name.empty()) event.name = name;
                event.detail = block.contains("input") ? tool_detail(block["input"]) : "";
                event.text = block.contains("input") ? block["input"].dump() : "";
                events.push_back(std::move(event));
            }
        }
    } else if (type == "user" && input.contains("message")) {
        for (const auto& block : input["message"].value("content", json::array())) {
            if (string_at(block, "type") != "tool_result") continue;
            auto event = base_event(input, EventKind::ToolFinished);
            event.tool_id = string_at(block, "tool_use_id");
            event.is_error = block.value("is_error", false);
            event.text = content_text(block.value("content", json()));
            events.push_back(std::move(event));
        }
    } else if (type == "result") {
        auto event = base_event(input, EventKind::RunFinished);
        event.text = input.contains("structured_output") ? input["structured_output"].dump() : string_at(input, "result");
        event.detail = string_at(input, "subtype");
        event.is_error = input.value("is_error", false);
        apply_usage(event, input);
        events.push_back(std::move(event));
    } else if (type == "error" || (type == "system" && string_at(input, "level") == "error")) {
        auto event = base_event(input, EventKind::Error);
        event.text = string_at(input, "content");
        if (event.text.empty()) event.text = string_at(input, "error");
        event.is_error = true;
        events.push_back(std::move(event));
    }
    return events;
}

std::vector<ClaudeEvent> KimiEventParser::parse(const std::string& line) const {
    std::vector<ClaudeEvent> events;
    const json input = json::parse(line);
    const auto role = string_at(input, "role");
    const auto type = string_at(input, "type");

    if (role == "assistant" && input.contains("tool_calls")) {
        for (const auto& call : input["tool_calls"]) {
            const auto& function = call["function"];
            const auto name = string_at(function, "name");
            const auto arguments = function.contains("arguments") && function["arguments"].is_string()
                ? json::parse(function["arguments"].get<std::string>()) : json::object();
            const bool subagent = name == "Agent" || name == "AgentSwarm";
            auto event = base_event(input, subagent ? EventKind::SubagentStarted : EventKind::ToolStarted);
            event.tool_id = string_at(call, "id");
            event.name = subagent ? string_at(arguments, "subagent_type") : name;
            if (event.name.empty()) event.name = name;
            event.detail = tool_detail(arguments);
            event.text = arguments.dump();
            events.push_back(std::move(event));
        }
    } else if (role == "assistant" && input.contains("content") && input["content"].is_string()) {
        auto event = base_event(input, EventKind::AssistantMessage);
        event.text = input["content"].get<std::string>();
        if (!event.text.empty()) events.push_back(std::move(event));
    } else if (role == "tool") {
        auto event = base_event(input, EventKind::ToolFinished);
        event.tool_id = string_at(input, "tool_call_id");
        event.text = string_at(input, "content");
        event.is_error = event.text.find("status: failed") != std::string::npos;
        events.push_back(std::move(event));
    } else if (type == "session.resume_hint") {
        auto started = base_event(input, EventKind::SessionStarted);
        started.name = "Kimi Code";
        events.push_back(started);
        auto finished = base_event(input, EventKind::RunFinished);
        finished.detail = "success";
        apply_usage(finished, input);
        events.push_back(std::move(finished));
    } else if (role == "error" || type == "error") {
        auto event = base_event(input, EventKind::Error);
        event.text = string_at(input, "content");
        event.is_error = true;
        events.push_back(std::move(event));
    }
    return events;
}

std::vector<ClaudeEvent> KimiAcpEventParser::parse(const std::string& line) const {
    std::vector<ClaudeEvent> events;
    const json input = json::parse(line);
    if (string_at(input, "method") != "session/update" || !input.contains("params")) return events;
    const auto& params = input["params"];
    if (!params.contains("update")) return events;
    const auto& update = params["update"];
    const auto type = string_at(update, "sessionUpdate");
    const auto base = [&](EventKind kind) {
        ClaudeEvent event;
        event.kind = kind;
        event.session_id = string_at(params, "sessionId");
        return event;
    };

    if (type == "agent_thought_chunk" || type == "agent_message_chunk") {
        auto event = base(type == "agent_thought_chunk" ? EventKind::ThoughtDelta : EventKind::AssistantDelta);
        if (update.contains("content")) event.text = content_text(update["content"]);
        if (!event.text.empty()) events.push_back(std::move(event));
    } else if (type == "tool_call") {
        const auto name = string_at(update, "title");
        const bool subagent = name == "Agent" || name == "AgentSwarm" || name == "Task";
        auto event = base(subagent ? EventKind::SubagentStarted : EventKind::ToolStarted);
        event.tool_id = string_at(update, "toolCallId");
        event.name = name;
        if (event.name.empty()) event.name = string_at(update, "kind");
        if (update.contains("rawInput")) {
            if (subagent) {
                const auto agent_name = string_at(update["rawInput"], "subagent_type");
                if (!agent_name.empty()) event.name = agent_name;
            }
            event.detail = tool_detail(update["rawInput"]);
            event.text = update["rawInput"].dump();
        }
        events.push_back(std::move(event));
    } else if (type == "tool_call_update") {
        const auto status = string_at(update, "status");
        if (status == "in_progress" && update.contains("rawInput")) {
            auto event = base(EventKind::ToolStarted);
            event.tool_id = string_at(update, "toolCallId");
            event.name = string_at(update, "title");
            if (event.name.empty()) event.name = string_at(update, "kind");
            event.detail = tool_detail(update["rawInput"]);
            event.text = update["rawInput"].dump();
            events.push_back(std::move(event));
            return events;
        }
        if (status != "completed" && status != "failed" && status != "cancelled") return events;
        auto event = base(EventKind::ToolFinished);
        event.tool_id = string_at(update, "toolCallId");
        event.is_error = status != "completed";
        if (update.contains("content")) event.text = content_text(update["content"]);
        if (event.text.empty() && update.contains("rawOutput")) {
            event.text = update["rawOutput"].is_string() ? update["rawOutput"].get<std::string>() : update["rawOutput"].dump();
        }
        events.push_back(std::move(event));
    }
    return events;
}

std::string wiki_schema() {
    json schema;
    schema["type"] = "object";
    schema["additionalProperties"] = false;
    schema["required"] = {"summary", "edits", "graph"};
    schema["properties"]["summary"] = {{"type", "string"}};
    schema["properties"]["edits"] = {
        {"type", "array"},
        {"items", {
            {"type", "object"},
            {"additionalProperties", false},
            {"required", {"path", "content"}},
            {"properties", {{"path", {{"type", "string"}}}, {"content", {{"type", "string"}}}}}
        }}
    };
    schema["properties"]["graph"] = {
        {"type", "array"},
        {"items", {
            {"type", "object"},
            {"additionalProperties", false},
            {"required", {"op", "id", "type", "source", "confidence", "properties"}},
            {"properties", {
                {"op", {{"type", "string"}, {"enum", {"upsert_entity", "relate"}}}},
                {"id", {{"type", "string"}}},
                {"type", {{"type", "string"}}},
                {"name", {{"type", "string"}}},
                {"source_id", {{"type", "string"}}},
                {"target_id", {{"type", "string"}}},
                {"source", {{"type", "string"}}},
                {"confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                {"properties", {{"type", "object"}, {"additionalProperties", {{"type", "string"}}}}}
            }}
        }}
    };
    return schema.dump();
}

std::string claude_agents_json(const std::vector<AgentDefinition>& agents) {
    json definitions = json::object();
    for (const auto& agent : agents) definitions[agent.name] = claude_agent_definition(agent);
    return definitions.dump();
}

namespace {

// True when `name` resolves to an executable on PATH, mirroring what execvp will search.
bool executable_on_path(const std::string& name) {
    if (name.find('/') != std::string::npos) return access(name.c_str(), X_OK) == 0;
    const char* path = std::getenv("PATH");
    std::stringstream directories(path ? path : "/usr/bin:/bin");
    std::string directory;
    while (std::getline(directories, directory, ':')) {
        const auto candidate = (directory.empty() ? std::string(".") : directory) + "/" + name;
        struct stat info {};
        if (stat(candidate.c_str(), &info) == 0 && S_ISREG(info.st_mode) && access(candidate.c_str(), X_OK) == 0) return true;
    }
    return false;
}

} // namespace

struct ClaudeProcess::Impl {
    mutable std::mutex mutex;
    std::deque<ClaudeEvent> events;
    std::thread worker;
    std::atomic<bool> running{false};
    pid_t pid = -1;

    void push(std::vector<ClaudeEvent> incoming) {
        if (incoming.empty()) return;
        {
            std::lock_guard lock(mutex);
            for (auto& event : incoming) events.push_back(std::move(event));
        }
        Rwake_up();
    }

    void push(ClaudeEvent event) {
        {
            std::lock_guard lock(mutex);
            events.push_back(std::move(event));
        }
        Rwake_up();
    }
};

ClaudeProcess::ClaudeProcess() : impl_(std::make_unique<Impl>()) {}

ClaudeProcess::~ClaudeProcess() { stop(); }

bool ClaudeProcess::start(const RunOptions& options, std::string& error) {
    if (running()) {
        error = "A Claude run is already active";
        return false;
    }
    if (impl_->worker.joinable()) impl_->worker.join();
    if (options.prompt.empty()) {
        error = "A prompt is required";
        return false;
    }

    std::string agent_definitions;
    if (options.harness == Harness::ClaudeCode) {
        try {
            agent_definitions = claude_agents_json(Workspace(options.working_directory).agents());
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }

    // Fail before forking with a message the user can act on. A missing CLI otherwise surfaces
    // only as "exited with status 127".
    const char* executable = options.harness == Harness::KimiCode ? "kimi" : "claude";
    if (!executable_on_path(executable)) {
        error = std::string(options.harness == Harness::KimiCode ? "Kimi Code" : "Claude Code") + " was not found: `" +
            executable + "` is not on PATH. Install the CLI, or start Stellar from a shell where it is available.";
        return false;
    }

    const bool acp = options.harness == Harness::KimiCode && !options.wiki_schema;
    int input_pipe[2];
    if (acp && pipe(input_pipe) != 0) {
        error = std::strerror(errno);
        return false;
    }
    int output_pipe[2];
    if (pipe(output_pipe) != 0) {
        if (acp) {
            close(input_pipe[0]);
            close(input_pipe[1]);
        }
        error = std::strerror(errno);
        return false;
    }

    std::vector<std::string> arguments;
    if (acp) {
        arguments = {"kimi"};
        if (!options.model.empty()) arguments.insert(arguments.end(), {"--model", options.model});
        if (options.session_id.empty() && !options.agent_file.empty()) {
            arguments.insert(arguments.end(), {"--agent-file", options.agent_file.string()});
        }
        arguments.push_back("acp");
    } else if (options.harness == Harness::KimiCode) {
        arguments = {"kimi", "-p", "\n" + options.prompt, "--output-format", "stream-json"};
        if (!options.model.empty()) arguments.insert(arguments.end(), {"--model", options.model});
        if (!options.session_id.empty()) arguments.insert(arguments.end(), {"--session", options.session_id});
        else if (!options.agent_file.empty()) arguments.insert(arguments.end(), {"--agent-file", options.agent_file.string()});
    } else {
        arguments = {
            "claude", "-p", "--output-format", "stream-json", "--verbose",
            "--include-partial-messages", "--forward-subagent-text", "--prompt-suggestions", "false",
            "--permission-prompts", "none"
        };
        if (!options.model.empty()) arguments.insert(arguments.end(), {"--model", options.model});
        if (!options.effort.empty()) arguments.insert(arguments.end(), {"--effort", options.effort});
        // Every --allowedTools value is collected and passed once at the end.
        std::vector<std::string> allowed_tools;
        if (options.read_only) {
            arguments.insert(arguments.end(), {"--tools", options.wiki_schema ? "" : "Read,Glob,Grep"});
        } else {
            arguments.insert(arguments.end(), {"--permission-mode", "acceptEdits"});
            // acceptEdits only covers file edits. Nothing can answer a permission prompt in a
            // headless run, so without this every build and test command is denied and a Build
            // run can never verify its own change. Kimi runs already approve tool calls.
            allowed_tools.push_back("Bash");
        }
        if (!agent_definitions.empty()) arguments.insert(arguments.end(), {"--agents", agent_definitions});
        if (!options.agent.empty()) arguments.insert(arguments.end(), {"--agent", options.agent});
        if (!options.session_id.empty()) arguments.insert(arguments.end(), {"--resume", options.session_id});
        if (options.wiki_schema) {
            allowed_tools.push_back("StructuredOutput");
            arguments.insert(arguments.end(), {"--json-schema", wiki_schema()});
        }
        if (!options.knowledge_binary.empty()) allowed_tools.push_back("Bash(" + options.knowledge_binary.string() + " *)");
        if (!allowed_tools.empty()) {
            arguments.push_back("--allowedTools");
            arguments.insert(arguments.end(), allowed_tools.begin(), allowed_tools.end());
        }
    }
    if (options.harness == Harness::ClaudeCode) arguments.insert(arguments.end(), {"--", options.prompt});

    if (std::getenv("RAVEN_IDE_DEBUG")) {
        std::fprintf(stderr, "[Harness launch]");
        for (const auto& argument : arguments) {
            const bool prompt = argument == options.prompt || argument == "\n" + options.prompt;
            std::fprintf(stderr, " %s", prompt ? "<prompt>" : argument.c_str());
        }
        std::fprintf(stderr, "\n");
    }

    const auto pid = fork();
    if (pid < 0) {
        error = std::strerror(errno);
        if (acp) {
            close(input_pipe[0]);
            close(input_pipe[1]);
        }
        close(output_pipe[0]);
        close(output_pipe[1]);
        return false;
    }
    if (pid == 0) {
        // Own process group: Stop and shutdown signal the whole group, so commands the agent
        // started (builds, test runners, dev servers) do not outlive the run, and Ctrl+C in the
        // launching terminal is delivered to Stellar rather than straight to the harness.
        setpgid(0, 0);
        // Stellar blocks SIGINT/SIGTERM for its own shutdown handling; the mask is inherited, and
        // a harness that kept it could not be stopped.
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        if (chdir(options.working_directory.c_str()) != 0) _exit(126);
        if (acp) {
            dup2(input_pipe[0], STDIN_FILENO);
            close(input_pipe[0]);
            close(input_pipe[1]);
        } else {
            // A headless run must never wait on the terminal Stellar was started from.
            const int null_input = open("/dev/null", O_RDONLY);
            if (null_input >= 0) {
                dup2(null_input, STDIN_FILENO);
                close(null_input);
            }
        }
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(output_pipe[0]);
        close(output_pipe[1]);
        std::vector<char*> argv;
        for (auto& argument : arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    // Also set from the parent so the group exists before stop() can run.
    setpgid(pid, pid);
    if (acp) close(input_pipe[0]);
    close(output_pipe[1]);
    impl_->pid = pid;
    impl_->running = true;
    impl_->worker = std::thread([this, fd = output_pipe[0], input_fd = acp ? input_pipe[1] : -1, pid,
        harness = options.harness, options, acp] {
        std::string buffer;
        char chunk[8192];
        ClaudeEventParser claude_parser;
        KimiEventParser kimi_parser;
        const auto parse = [&](const std::string& line) {
            return harness == Harness::KimiCode ? kimi_parser.parse(line) : claude_parser.parse(line);
        };
        bool received_result = false;
        if (acp) {
            received_result = run_kimi_acp(fd, input_fd, options);
            close(input_fd);
        } else {
            while (true) {
                const auto count = read(fd, chunk, sizeof(chunk));
                if (count <= 0) break;
                buffer.append(chunk, static_cast<size_t>(count));
                size_t newline;
                while ((newline = buffer.find('\n')) != std::string::npos) {
                    auto line = buffer.substr(0, newline);
                    buffer.erase(0, newline + 1);
                    if (line.empty()) continue;
                    try {
                        auto events = parse(line);
                        for (const auto& event : events) received_result |= event.kind == EventKind::RunFinished;
                        impl_->push(std::move(events));
                    } catch (const std::exception&) {
                        if (harness == Harness::KimiCode && line.rfind("error", 0) != 0 && line.rfind("Error", 0) != 0) continue;
                        ClaudeEvent event;
                        event.kind = EventKind::Error;
                        event.text = line;
                        impl_->push(std::move(event));
                    }
                }
            }
        }
        close(fd);
        if (!buffer.empty()) {
            try {
                impl_->push(parse(buffer));
            } catch (const std::exception&) {
            }
        }
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        if (!received_result && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
            ClaudeEvent event;
            event.kind = EventKind::RunFinished;
            event.is_error = true;
            event.detail = "process_failed";
            event.text = WIFEXITED(status)
                ? std::string(harness == Harness::KimiCode ? "Kimi Code" : "Claude Code") + " exited with status " + std::to_string(WEXITSTATUS(status))
                : std::string(harness == Harness::KimiCode ? "Kimi Code" : "Claude Code") + " stopped unexpectedly";
            impl_->push(std::move(event));
        }
        impl_->running = false;
        Rwake_up();
    });
    return true;
}

bool ClaudeProcess::run_kimi_acp(int output_fd, int input_fd, const RunOptions& options) {
    KimiAcpEventParser parser;
    std::string buffer;
    std::string diagnostics;
    auto fail = [&](const std::string& text) {
        ClaudeEvent event;
        event.kind = EventKind::RunFinished;
        event.session_id = options.session_id;
        event.is_error = true;
        event.detail = "acp_error";
        event.text = diagnostics.empty() ? text : text + "\n" + diagnostics;
        impl_->push(std::move(event));
        return true;
    };
    auto send = [&](const json& request) {
        const auto data = request.dump() + "\n";
        size_t written = 0;
        while (written < data.size()) {
            const auto count = write(input_fd, data.data() + written, data.size() - written);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            written += static_cast<size_t>(count);
        }
        return true;
    };
    auto read_line = [&](std::string& line) {
        while (true) {
            const auto newline = buffer.find('\n');
            if (newline != std::string::npos) {
                line = buffer.substr(0, newline);
                buffer.erase(0, newline + 1);
                return true;
            }
            char chunk[8192];
            const auto count = read(output_fd, chunk, sizeof(chunk));
            if (count <= 0) return false;
            buffer.append(chunk, static_cast<size_t>(count));
        }
    };
    auto read_response = [&](int id, json& response) {
        std::string line;
        while (read_line(line)) {
            json input;
            try {
                input = json::parse(line);
            } catch (const std::exception&) {
                if (!line.empty()) {
                    if (!diagnostics.empty()) diagnostics += "\n";
                    diagnostics += line;
                    if (diagnostics.size() > 8192) diagnostics.erase(0, diagnostics.size() - 8192);
                }
                continue;
            }
            if (string_at(input, "method") == "session/request_permission" && input.contains("id")) {
                std::string option_id;
                if (input.contains("params") && input["params"].contains("options")) {
                    for (const auto& option : input["params"]["options"]) {
                        const auto candidate = string_at(option, "optionId");
                        if (option_id.empty()) option_id = candidate;
                        if (string_at(option, "kind") == "allow_once") {
                            option_id = candidate;
                            break;
                        }
                    }
                }
                json outcome = option_id.empty()
                    ? json({{"outcome", "cancelled"}})
                    : json({{"outcome", "selected"}, {"optionId", option_id}});
                send({{"jsonrpc", "2.0"}, {"id", input["id"]}, {"result", {{"outcome", outcome}}}});
                continue;
            }
            if (input.contains("id") && input["id"].is_number_integer() && input["id"].get<int>() == id) {
                response = std::move(input);
                return true;
            }
            try {
                impl_->push(parser.parse(line));
            } catch (const std::exception&) {
            }
        }
        return false;
    };
    auto response_ok = [&](const json& response) {
        if (!response.contains("error")) return true;
        fail(string_at(response["error"], "message"));
        return false;
    };

    send({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {
        {"protocolVersion", 1}, {"clientInfo", {{"name", "Stellar"}, {"version", "0.1.0"}}},
        {"clientCapabilities", json::object()}
    }}});
    json response;
    if (!read_response(1, response)) return fail("Kimi Code closed the ACP connection during initialization");
    if (!response_ok(response)) return true;

    const auto method = options.session_id.empty() ? "session/new" : "session/resume";
    json params = {{"cwd", options.working_directory.string()}, {"mcpServers", json::array()}};
    if (!options.session_id.empty()) params["sessionId"] = options.session_id;
    send({{"jsonrpc", "2.0"}, {"id", 2}, {"method", method}, {"params", params}});
    if (!read_response(2, response)) return fail("Kimi Code closed the ACP connection while opening the session");
    if (!response_ok(response)) return true;

    auto session_id = options.session_id;
    if (response.contains("result")) {
        const auto created = string_at(response["result"], "sessionId");
        if (!created.empty()) session_id = created;
    }
    if (session_id.empty()) return fail("Kimi Code did not return an ACP session ID");
    ClaudeEvent started;
    started.kind = EventKind::SessionStarted;
    started.session_id = session_id;
    started.name = "Kimi Code";
    impl_->push(std::move(started));

    send({{"jsonrpc", "2.0"}, {"method", "session/set_mode"}, {"params", {{"sessionId", session_id}, {"modeId", "auto"}}}});
    auto set_config = [&](int request_id, const char* config_id, const std::string& value) {
        if (value.empty()) return true;
        send({{"jsonrpc", "2.0"}, {"id", request_id}, {"method", "session/set_config_option"}, {"params", {
            {"sessionId", session_id}, {"configId", config_id}, {"value", value}
        }}});
        if (!read_response(request_id, response)) {
            fail(std::string("Kimi Code closed the ACP connection while setting ") + config_id);
            return false;
        }
        return response_ok(response);
    };
    if (!set_config(3, "model", options.model) || !set_config(4, "thinking", options.effort)) return true;
    json prompt_blocks = json::array({{{"type", "text"}, {"text", options.prompt}}});
    for (const auto& path : options.image_links) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0 || size > 20 * 1024 * 1024) {
            prompt_blocks.push_back({{"type", "resource_link"}, {"uri", path.string()}, {"name", path.filename().string()}});
            continue;
        }
        std::ifstream input(path, std::ios::binary);
        const std::string data{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if ((input.good() || input.eof()) && data.size() == size) {
            prompt_blocks.push_back({{"type", "image"}, {"data", base64_encode(data)}, {"mimeType", image_mime_type(path)}});
        } else {
            prompt_blocks.push_back({{"type", "resource_link"}, {"uri", path.string()}, {"name", path.filename().string()}});
        }
    }
    for (const auto& path : options.resource_links) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (!error && size <= 512 * 1024) {
            std::ifstream input(path, std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            if ((input.good() || input.eof()) && text.find('\0') == std::string::npos) {
                json block = json::object();
                block["type"] = "resource";
                block["resource"] = json::object({{"uri", path.string()}, {"mimeType", "text/plain"}, {"text", text}});
                prompt_blocks.push_back(std::move(block));
                continue;
            }
        }
        prompt_blocks.push_back({{"type", "resource_link"}, {"uri", path.string()}, {"name", path.filename().string()}});
    }
    send({{"jsonrpc", "2.0"}, {"id", 5}, {"method", "session/prompt"}, {"params", {
        {"sessionId", session_id}, {"prompt", prompt_blocks}
    }}});
    if (!read_response(5, response)) return fail("Kimi Code closed the ACP connection before completing the run");
    if (!response_ok(response)) return true;

    ClaudeEvent finished;
    finished.kind = EventKind::RunFinished;
    finished.session_id = session_id;
    finished.detail = response.contains("result") ? string_at(response["result"], "stopReason") : "success";
    if (response.contains("result")) apply_usage(finished, response["result"]);
    impl_->push(std::move(finished));
    return true;
}

void ClaudeProcess::stop() {
    if (impl_->pid > 0 && impl_->running.exchange(false)) {
        if (kill(-impl_->pid, SIGTERM) != 0) kill(impl_->pid, SIGTERM);
    }
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->pid = -1;
}

bool ClaudeProcess::running() const { return impl_->running.load(); }

std::deque<ClaudeEvent> ClaudeProcess::drain() {
    std::lock_guard lock(impl_->mutex);
    auto result = std::move(impl_->events);
    impl_->events.clear();
    return result;
}

} // namespace raven
