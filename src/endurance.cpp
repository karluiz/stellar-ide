#include "endurance.hpp"

#include "workspace.hpp"

#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace raven {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string join(const std::vector<std::string>& values, const std::string& separator = ", ") {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result += separator;
        result += value;
    }
    return result;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string slug(std::string value) {
    std::string result;
    for (const unsigned char c : value) {
        if (std::isalnum(c)) result.push_back(static_cast<char>(std::tolower(c)));
        else if (!result.empty() && result.back() != '-') result.push_back('-');
    }
    while (!result.empty() && result.back() == '-') result.pop_back();
    return result.empty() ? "mission" : result;
}

void allowed_keys(const YAML::Node& node, const std::set<std::string>& allowed, const std::string& context) {
    if (!node.IsMap()) throw std::runtime_error(context + " must be a mapping");
    for (const auto& item : node) {
        const auto key = item.first.as<std::string>();
        if (!allowed.count(key)) throw std::runtime_error(context + " contains unknown field '" + key + "'");
    }
}

std::string value(const YAML::Node& node, const char* key, const char* fallback) {
    return node[key] ? node[key].as<std::string>() : fallback;
}

template <typename T>
T value(const YAML::Node& node, const char* key, const T& fallback) {
    return node[key] ? node[key].as<T>() : fallback;
}

std::string bounded(std::string value, size_t limit = 64 * 1024) {
    if (value.size() > limit) value.resize(limit);
    return value;
}

json step_result_json(const EnduranceStepResult& result) {
    return {
        {"status", result.status}, {"attempt", result.attempt}, {"session_id", result.session_id},
        {"output", result.output}, {"error", result.error}, {"checkpoint_id", result.checkpoint_id},
        {"started_at", result.started_at}, {"finished_at", result.finished_at}, {"cost_usd", result.cost_usd},
        {"input_tokens", result.input_tokens}, {"output_tokens", result.output_tokens}
    };
}

EnduranceStepResult parse_step_result(const json& root) {
    EnduranceStepResult result;
    result.status = root.value("status", result.status);
    result.attempt = root.value("attempt", 0);
    result.session_id = root.value("session_id", "");
    result.output = bounded(root.value("output", ""));
    result.error = root.value("error", "");
    result.checkpoint_id = root.value("checkpoint_id", "");
    result.started_at = root.value("started_at", 0);
    result.finished_at = root.value("finished_at", 0);
    result.cost_usd = root.value("cost_usd", -1.0);
    result.input_tokens = root.value("input_tokens", 0);
    result.output_tokens = root.value("output_tokens", 0);
    return result;
}

bool has_path(const EnduranceWorkflow& workflow, const std::string& from, const std::string& to, std::set<std::string>& visited) {
    if (from == to) return true;
    if (!visited.insert(from).second) return false;
    for (const auto& step : workflow.steps) {
        if (step.id != from) continue;
        for (const auto& need : step.needs) {
            if (has_path(workflow, need, to, visited)) return true;
        }
    }
    return false;
}

bool depends_on(const EnduranceWorkflow& workflow, const std::string& step, const std::string& ancestor) {
    std::set<std::string> visited;
    return has_path(workflow, step, ancestor, visited);
}

struct CronField {
    std::set<int> values;
    bool wildcard = false;
};

CronField cron_field(const std::string& source, int minimum, int maximum, bool weekday = false) {
    CronField field;
    if (source.empty()) throw std::runtime_error("Empty cron field");
    std::stringstream parts(source);
    std::string part;
    while (std::getline(parts, part, ',')) {
        const auto slash = part.find('/');
        const int step = slash == std::string::npos ? 1 : std::stoi(part.substr(slash + 1));
        if (step <= 0) throw std::runtime_error("Cron step must be positive");
        const auto range = slash == std::string::npos ? part : part.substr(0, slash);
        int first = minimum, last = maximum;
        if (range != "*") {
            const auto dash = range.find('-');
            if (dash == std::string::npos) {
                first = last = std::stoi(range);
            } else {
                first = std::stoi(range.substr(0, dash));
                last = std::stoi(range.substr(dash + 1));
            }
        }
        if (weekday && range != "*") {
            if (first == 7) first = 0;
            if (last == 7) last = 0;
        }
        if (first < minimum || last > maximum || first > last) throw std::runtime_error("Cron field is out of range: " + range);
        for (int value = first; value <= last; value += step) field.values.insert(weekday && value == 7 ? 0 : value);
    }
    field.wildcard = source == "*" || source == "*/1";
    if (field.values.empty()) throw std::runtime_error("Cron field has no values");
    return field;
}

bool cron_time_matches(const std::array<CronField, 5>& fields, const std::tm& time) {
    return fields[0].values.count(time.tm_min) && fields[1].values.count(time.tm_hour) &&
        fields[2].values.count(time.tm_mday) && fields[3].values.count(time.tm_mon + 1) && fields[4].values.count(time.tm_wday);
}

} // namespace

EnduranceWorkflow endurance_default_workflow() {
    EnduranceWorkflow workflow;
    workflow.name = "Ship with confidence";
    workflow.description = "Research, review, approve, implement, and verify a task.";
    workflow.input_default = "Review the current workspace and implement the next safe, verifiable improvement.";
    workflow.recurrence.enabled = false;
    workflow.steps = {
        {"research", "agent", "orchestrator", "", "ask", "", "", "workspace",
            "Investigate this task and report the relevant implementation context:\n${{ inputs.task }}", {}, 40, 100, 900, 0},
        {"review", "agent", "code-reviewer", "claude", "review", "", "", "workspace",
            "Review the proposed approach and identify material risks:\n${{ steps.research.output }}", {"research"}, 290, 100, 900, 0},
        {"approval", "approval", "", "", "", "", "", "workspace",
            "Approve implementation after reviewing the research and risks.", {"review"}, 540, 100, 900, 0},
        {"implement", "agent", "orchestrator", "", "build", "", "", "workspace",
            "Implement the approved task:\n${{ inputs.task }}\n\nResearch:\n${{ steps.research.output }}\n\nReview:\n${{ steps.review.output }}",
            {"approval"}, 790, 100, 900, 0},
        {"verify", "agent", "code-reviewer", "claude", "review", "", "", "workspace",
            "Review the implementation and verification evidence:\n${{ steps.implement.output }}", {"implement"}, 1040, 100, 900, 0}
    };
    return workflow;
}

std::string endurance_workflow_yaml(const EnduranceWorkflow& workflow) {
    YAML::Node root;
    root["version"] = 1;
    root["name"] = workflow.name;
    if (!workflow.description.empty()) root["description"] = workflow.description;
    root["defaults"]["harness"] = workflow.default_harness;
    root["concurrency"] = workflow.concurrency;
    if (workflow.recurrence.enabled) {
        root["trigger"]["type"] = "cron";
        root["trigger"]["expression"] = workflow.recurrence.expression;
        root["trigger"]["timezone"] = workflow.recurrence.timezone;
        root["trigger"]["enabled"] = true;
        root["trigger"]["overlap"] = workflow.recurrence.overlap;
        root["trigger"]["missed_runs"] = workflow.recurrence.missed_runs;
    }
    root["inputs"]["task"]["type"] = "string";
    root["inputs"]["task"]["required"] = workflow.input_required;
    if (!workflow.input_default.empty()) root["inputs"]["task"]["default"] = workflow.input_default;
    for (const auto& step : workflow.steps) {
        YAML::Node node;
        node["id"] = step.id;
        node["type"] = step.type;
        if (step.type == "agent") {
            node["agent"] = step.agent;
            if (!step.harness.empty()) node["harness"] = step.harness;
            node["mode"] = step.mode;
            if (!step.model.empty()) node["model"] = step.model;
            if (!step.effort.empty()) node["effort"] = step.effort;
        }
        if (step.isolation != "workspace") node["isolation"] = step.isolation;
        if (!step.needs.empty()) {
            YAML::Node needs;
            for (const auto& need : step.needs) needs.push_back(need);
            needs.SetStyle(YAML::EmitterStyle::Flow);
            node["needs"] = needs;
        }
        YAML::Node position;
        position.push_back(static_cast<int>(std::lround(step.x)));
        position.push_back(static_cast<int>(std::lround(step.y)));
        position.SetStyle(YAML::EmitterStyle::Flow);
        node["position"] = position;
        if (step.timeout_seconds != 900) node["timeout_seconds"] = step.timeout_seconds;
        if (step.retries != 0) node["retries"] = step.retries;
        YAML::Node prompt(step.prompt);
        node["prompt"] = prompt;
        root["steps"].push_back(node);
    }
    YAML::Emitter emitter;
    emitter << root;
    return std::string(emitter.c_str()) + "\n";
}

EnduranceWorkflow endurance_parse_workflow(const std::string& source, const fs::path& path) {
    EnduranceWorkflow workflow;
    workflow.path = path;
    const auto root = YAML::Load(source);
    allowed_keys(root, {"version", "name", "description", "defaults", "concurrency", "trigger", "inputs", "steps"}, "Workflow");
    if (value(root, "version", 0) != 1) throw std::runtime_error("Workflow version must be 1");
    workflow.name = value(root, "name", workflow.name);
    workflow.description = value(root, "description", "");
    if (root["defaults"]) {
        allowed_keys(root["defaults"], {"harness", "timeout_seconds", "retries"}, "Defaults");
        workflow.default_harness = value(root["defaults"], "harness", workflow.default_harness);
    }
    workflow.concurrency = value(root, "concurrency", 2);
    if (root["trigger"]) {
        allowed_keys(root["trigger"], {"type", "expression", "timezone", "enabled", "overlap", "missed_runs"}, "Trigger");
        if (value(root["trigger"], "type", std::string("cron")) != "cron") throw std::runtime_error("Only cron triggers are supported");
        workflow.recurrence.enabled = value(root["trigger"], "enabled", true);
        workflow.recurrence.expression = value(root["trigger"], "expression", workflow.recurrence.expression);
        workflow.recurrence.timezone = value(root["trigger"], "timezone", "local");
        workflow.recurrence.overlap = value(root["trigger"], "overlap", "skip");
        workflow.recurrence.missed_runs = value(root["trigger"], "missed_runs", "skip");
    }
    if (root["inputs"]) {
        allowed_keys(root["inputs"], {"task"}, "Inputs");
        const auto task = root["inputs"]["task"];
        allowed_keys(task, {"type", "required", "default"}, "Task input");
        if (value(task, "type", std::string("string")) != "string") throw std::runtime_error("Task input must be a string");
        workflow.input_required = value(task, "required", true);
        workflow.input_default = value(task, "default", "");
    }
    if (!root["steps"] || !root["steps"].IsSequence()) throw std::runtime_error("Workflow steps must be a sequence");
    const int default_timeout = root["defaults"] ? value(root["defaults"], "timeout_seconds", 900) : 900;
    const int default_retries = root["defaults"] ? value(root["defaults"], "retries", 0) : 0;
    for (const auto& item : root["steps"]) {
        allowed_keys(item, {"id", "type", "agent", "harness", "mode", "model", "effort", "isolation", "needs", "position", "timeout_seconds", "retries", "prompt"}, "Step");
        EnduranceStep step;
        step.id = value(item, "id", std::string());
        step.type = value(item, "type", std::string("agent"));
        step.agent = value(item, "agent", std::string("orchestrator"));
        step.harness = value(item, "harness", "");
        step.mode = value(item, "mode", step.type == "approval" ? "" : "ask");
        step.model = value(item, "model", "");
        step.effort = value(item, "effort", "");
        step.isolation = value(item, "isolation", "workspace");
        step.prompt = value(item, "prompt", "");
        step.timeout_seconds = value(item, "timeout_seconds", default_timeout);
        step.retries = value(item, "retries", default_retries);
        if (item["needs"]) {
            if (item["needs"].IsScalar()) step.needs.push_back(item["needs"].as<std::string>());
            else if (item["needs"].IsSequence()) step.needs = item["needs"].as<std::vector<std::string>>();
            else throw std::runtime_error("Step " + step.id + " needs must be a string or sequence");
        }
        if (item["position"]) {
            const auto position = item["position"].as<std::vector<float>>();
            if (position.size() != 2) throw std::runtime_error("Step " + step.id + " position must contain x and y");
            step.x = position[0];
            step.y = position[1];
        }
        workflow.steps.push_back(std::move(step));
    }
    if (const auto errors = endurance_validate(workflow); !errors.empty()) throw std::runtime_error(join(errors, "\n"));
    return workflow;
}

std::vector<std::string> endurance_validate(const EnduranceWorkflow& workflow) {
    std::vector<std::string> errors;
    if (trim(workflow.name).empty()) errors.push_back("Workflow name is required");
    if (workflow.default_harness != "kimi" && workflow.default_harness != "claude") errors.push_back("Default harness must be kimi or claude");
    if (workflow.concurrency < 1 || workflow.concurrency > 4) errors.push_back("Concurrency must be between 1 and 4");
    if (workflow.steps.empty()) errors.push_back("At least one step is required");
    if (workflow.steps.size() > 100) errors.push_back("Workflow is limited to 100 steps");
    if (workflow.recurrence.timezone != "local" && workflow.recurrence.timezone != "UTC") errors.push_back("Trigger timezone must be local or UTC");
    if (workflow.recurrence.overlap != "skip" && workflow.recurrence.overlap != "queue") errors.push_back("Trigger overlap must be skip or queue");
    if (workflow.recurrence.missed_runs != "skip" && workflow.recurrence.missed_runs != "run_once") errors.push_back("Trigger missed_runs must be skip or run_once");
    if (workflow.recurrence.enabled) {
        if (workflow.input_required && trim(workflow.input_default).empty()) errors.push_back("Recurring workflows require a default task input");
        try {
            (void)endurance_next_cron(workflow.recurrence.expression, endurance_now(), workflow.recurrence.timezone);
        } catch (const std::exception& error) {
            errors.push_back(std::string("Invalid cron trigger: ") + error.what());
        }
    }
    std::map<std::string, const EnduranceStep*> by_id;
    const std::regex id_pattern("[a-z][a-z0-9_-]{0,63}");
    for (const auto& step : workflow.steps) {
        if (!std::regex_match(step.id, id_pattern)) errors.push_back("Step ID is empty or invalid: " + step.id);
        if (by_id.count(step.id)) errors.push_back("Duplicate step ID: " + step.id);
        by_id[step.id] = &step;
        if (step.type != "agent" && step.type != "approval") errors.push_back("Step " + step.id + " has unsupported type " + step.type);
        if (step.type == "agent") {
            if (step.agent.empty()) errors.push_back("Agent is required for step " + step.id);
            if (step.mode != "ask" && step.mode != "review" && step.mode != "build") errors.push_back("Step " + step.id + " has invalid mode " + step.mode);
            if (!step.harness.empty() && step.harness != "kimi" && step.harness != "claude") errors.push_back("Step " + step.id + " has invalid harness " + step.harness);
        }
        if (step.isolation != "workspace" && step.isolation != "worktree") errors.push_back("Step " + step.id + " has invalid isolation " + step.isolation);
        if (step.timeout_seconds < 30 || step.timeout_seconds > 86400) errors.push_back("Step " + step.id + " timeout must be 30–86400 seconds");
        if (step.retries < 0 || step.retries > 5) errors.push_back("Step " + step.id + " retries must be 0–5");
        if (trim(step.prompt).empty()) errors.push_back("Step " + step.id + " prompt is required");
    }
    for (const auto& step : workflow.steps) {
        std::set<std::string> unique;
        for (const auto& need : step.needs) {
            if (!by_id.count(need)) errors.push_back("Step " + step.id + " depends on missing step " + need);
            if (need == step.id) errors.push_back("Step " + step.id + " depends on itself");
            if (!unique.insert(need).second) errors.push_back("Step " + step.id + " repeats dependency " + need);
        }
    }
    for (const auto& step : workflow.steps) {
        std::set<std::string> visiting, visited;
        std::function<void(const std::string&)> visit = [&](const std::string& id) {
            if (visiting.count(id)) throw std::runtime_error("cycle");
            if (visited.count(id)) return;
            visiting.insert(id);
            const auto found = by_id.find(id);
            if (found != by_id.end()) for (const auto& need : found->second->needs) visit(need);
            visiting.erase(id);
            visited.insert(id);
        };
        try { visit(step.id); } catch (...) { errors.push_back("Workflow contains a dependency cycle involving " + step.id); break; }
    }
    for (size_t first = 0; first < workflow.steps.size(); ++first) {
        if (workflow.steps[first].mode != "build") continue;
        for (size_t second = first + 1; second < workflow.steps.size(); ++second) {
            if (workflow.steps[second].mode != "build") continue;
            if (!depends_on(workflow, workflow.steps[first].id, workflow.steps[second].id) &&
                !depends_on(workflow, workflow.steps[second].id, workflow.steps[first].id)) {
                errors.push_back("Parallel Build steps are not allowed: " + workflow.steps[first].id + " and " + workflow.steps[second].id);
            }
        }
    }
    std::sort(errors.begin(), errors.end());
    errors.erase(std::unique(errors.begin(), errors.end()), errors.end());
    return errors;
}

EnduranceStepResult& endurance_result(EnduranceRun& run, const std::string& step_id) { return run.steps[step_id]; }
const EnduranceStepResult& endurance_result(const EnduranceRun& run, const std::string& step_id) {
    const auto found = run.steps.find(step_id);
    if (found == run.steps.end()) throw std::runtime_error("Missing run step " + step_id);
    return found->second;
}

bool endurance_dependencies_succeeded(const EnduranceRun& run, const EnduranceStep& step) {
    return std::all_of(step.needs.begin(), step.needs.end(), [&](const auto& need) {
        const auto found = run.steps.find(need);
        return found != run.steps.end() && found->second.status == "succeeded";
    });
}

bool endurance_is_terminal(const std::string& status) {
    return status == "succeeded" || status == "failed" || status == "stopped" || status == "blocked" || status == "skipped";
}

void endurance_block_downstream(EnduranceRun& run, const std::string& step_id) {
    for (const auto& step : run.snapshot.steps) {
        if (step.id == step_id || !depends_on(run.snapshot, step.id, step_id)) continue;
        auto& result = endurance_result(run, step.id);
        if (result.status == "ready" || result.status == "blocked") result.status = "blocked";
    }
}

void endurance_reset_for_resume(EnduranceRun& run) {
    for (const auto& step : run.snapshot.steps) {
        auto& result = endurance_result(run, step.id);
        if (result.status == "blocked") result.status = "ready";
        if (result.status == "failed" || result.status == "stopped") {
            result.status = "ready";
            result.output.clear();
            result.error.clear();
            result.session_id.clear();
            result.started_at = result.finished_at = 0;
        }
    }
    run.status = "running";
    run.error.clear();
    run.finished_at = 0;
}

std::string endurance_render_prompt(const EnduranceRun& run, const EnduranceStep& step) {
    std::string result = step.prompt;
    const auto replace_all = [&](const std::string& token, const std::string& value) {
        size_t position = 0;
        while ((position = result.find(token, position)) != std::string::npos) {
            result.replace(position, token.size(), value);
            position += value.size();
        }
    };
    replace_all("${{ inputs.task }}", run.input);
    for (const auto& other : run.snapshot.steps) {
        const auto found = run.steps.find(other.id);
        replace_all("${{ steps." + other.id + ".output }}", found == run.steps.end() ? "" : bounded(found->second.output));
    }
    if (result.find("${{") != std::string::npos) throw std::runtime_error("Unresolved template expression in step " + step.id);
    return result;
}

std::int64_t endurance_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::int64_t endurance_next_cron(const std::string& expression, std::int64_t after, const std::string& timezone) {
    if (timezone != "local" && timezone != "UTC") throw std::runtime_error("Timezone must be local or UTC");
    std::stringstream input(expression);
    std::vector<std::string> parts;
    std::string part;
    while (input >> part) parts.push_back(part);
    if (parts.size() != 5) throw std::runtime_error("Cron must contain five fields");
    const std::array<CronField, 5> fields = {
        cron_field(parts[0], 0, 59), cron_field(parts[1], 0, 23), cron_field(parts[2], 1, 31),
        cron_field(parts[3], 1, 12), cron_field(parts[4], 0, 7, true)
    };
    std::time_t candidate = static_cast<std::time_t>(after - after % 60 + 60);
    for (int minute = 0; minute < 527040; ++minute, candidate += 60) {
        std::tm time{};
        if (timezone == "UTC") gmtime_r(&candidate, &time); else localtime_r(&candidate, &time);
        if (cron_time_matches(fields, time)) return static_cast<std::int64_t>(candidate);
    }
    throw std::runtime_error("Cron has no occurrence in the next year");
}

EnduranceStore::EnduranceStore(fs::path workspace_root)
    : root_(std::move(workspace_root)), workflows_root_(root_ / "endurance"), runs_root_(root_ / ".stellar" / "endurance" / "runs") {}

std::vector<EnduranceWorkflow> EnduranceStore::load_workflows() const {
    std::vector<EnduranceWorkflow> result;
    std::error_code error;
    if (!fs::exists(workflows_root_)) return result;
    for (const auto& entry : fs::directory_iterator(workflows_root_, error)) {
        if (!entry.is_regular_file() || (entry.path().extension() != ".yaml" && entry.path().extension() != ".yml")) continue;
        result.push_back(endurance_parse_workflow(read_file(entry.path()), entry.path().filename()));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return result;
}

void EnduranceStore::save_workflow(const EnduranceWorkflow& workflow) const {
    const auto extension = workflow.path.extension();
    if (workflow.path.empty() || workflow.path.filename() != workflow.path || (extension != ".yaml" && extension != ".yml")) {
        throw std::runtime_error("Workflow path must be a .yaml or .yml filename inside endurance/");
    }
    write_file(workflows_root_ / workflow.path, endurance_workflow_yaml(workflow));
}

EnduranceWorkflow EnduranceStore::create_workflow() {
    auto workflow = endurance_default_workflow();
    int suffix = 1;
    do { workflow.path = "mission-" + std::to_string(suffix++) + ".yaml"; } while (fs::exists(workflows_root_ / workflow.path));
    save_workflow(workflow);
    return workflow;
}

EnduranceWorkflow EnduranceStore::duplicate_workflow(const EnduranceWorkflow& source) {
    auto workflow = source;
    workflow.name += " copy";
    const auto stem = source.path.stem().string();
    int suffix = 1;
    do { workflow.path = stem + "-copy-" + std::to_string(suffix++) + ".yaml"; } while (fs::exists(workflows_root_ / workflow.path));
    save_workflow(workflow);
    return workflow;
}

fs::path EnduranceStore::run_directory(const std::string& id) const {
    if (id.empty() || id.find_first_of("/\\.") != std::string::npos) throw std::runtime_error("Invalid Endurance run ID");
    return runs_root_ / id;
}

EnduranceRun EnduranceStore::create_run(const EnduranceWorkflow& workflow, const std::string& input, bool scheduled) const {
    EnduranceRun run;
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    run.id = "run-" + std::to_string(milliseconds);
    run.workflow_path = workflow.path.generic_string();
    run.workflow_name = workflow.name;
    run.input = bounded(input);
    run.snapshot = workflow;
    run.status = "running";
    run.created_at = run.updated_at = endurance_now();
    run.scheduled = scheduled;
    for (const auto& step : workflow.steps) run.steps.emplace(step.id, EnduranceStepResult{});
    save_run(run, "created");
    return run;
}

void EnduranceStore::save_run(const EnduranceRun& run, const std::string& event) const {
    const auto directory = run_directory(run.id);
    fs::create_directories(directory);
    json steps = json::object();
    for (const auto& [id, result] : run.steps) steps[id] = step_result_json(result);
    const json root = {
        {"version", 1}, {"id", run.id}, {"workflow_path", run.workflow_path}, {"workflow_name", run.workflow_name},
        {"input", run.input}, {"status", run.status}, {"error", run.error}, {"isolation", run.isolation},
        {"worktree_mission_id", run.worktree_mission_id}, {"snapshot", endurance_workflow_yaml(run.snapshot)},
        {"steps", steps}, {"created_at", run.created_at}, {"updated_at", run.updated_at},
        {"finished_at", run.finished_at}, {"scheduled", run.scheduled}
    };
    write_file(directory / "run.json", root.dump(2) + "\n");
    write_file(directory / "workflow.yaml", endurance_workflow_yaml(run.snapshot));
    for (const auto& [id, result] : run.steps) write_file(directory / "steps" / (id + ".md"), result.output);
    if (!event.empty()) {
        const json record = {{"time", endurance_now()}, {"event", event}, {"status", run.status}, {"error", run.error}};
        std::ofstream output(directory / "events.jsonl", std::ios::app);
        output << record.dump() << '\n';
    }
}

std::vector<EnduranceRun> EnduranceStore::load_runs() const {
    std::vector<EnduranceRun> result;
    std::error_code error;
    if (!fs::exists(runs_root_)) return result;
    for (const auto& entry : fs::directory_iterator(runs_root_, error)) {
        if (!entry.is_directory() || !fs::exists(entry.path() / "run.json")) continue;
        try { result.push_back(load_run(entry.path().filename().string())); } catch (...) {}
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.created_at > b.created_at; });
    if (result.size() > 100) result.resize(100);
    return result;
}

EnduranceRun EnduranceStore::load_run(const std::string& id) const {
    const auto root = json::parse(read_file(run_directory(id) / "run.json"));
    EnduranceRun run;
    run.id = root.at("id");
    if (run.id != id) throw std::runtime_error("Endurance run ID does not match its directory");
    run.workflow_path = root.value("workflow_path", "");
    run.workflow_name = root.value("workflow_name", "");
    run.input = bounded(root.value("input", ""));
    run.status = root.value("status", "ready");
    run.error = root.value("error", "");
    run.isolation = root.value("isolation", "workspace");
    run.worktree_mission_id = root.value("worktree_mission_id", "");
    run.snapshot = endurance_parse_workflow(root.at("snapshot").get<std::string>(), run.workflow_path);
    run.created_at = root.value("created_at", 0);
    run.updated_at = root.value("updated_at", 0);
    run.finished_at = root.value("finished_at", 0);
    run.scheduled = root.value("scheduled", false);
    for (const auto& step : run.snapshot.steps) {
        const auto stored = root["steps"].find(step.id);
        run.steps.emplace(step.id, stored == root["steps"].end() ? EnduranceStepResult{} : parse_step_result(*stored));
    }
    return run;
}

std::map<std::string, EnduranceScheduleState> EnduranceStore::load_schedule() const {
    std::map<std::string, EnduranceScheduleState> result;
    const auto path = root_ / ".stellar" / "endurance" / "schedule.json";
    if (!fs::exists(path)) return result;
    const auto root = json::parse(read_file(path));
    for (const auto& [workflow, state] : root.items()) {
        result[workflow] = {state.value("next_at", 0), state.value("last_at", 0), state.value("queued", false)};
    }
    return result;
}

void EnduranceStore::save_schedule(const std::map<std::string, EnduranceScheduleState>& state) const {
    json root = json::object();
    for (const auto& [workflow, schedule] : state) {
        root[workflow] = {{"next_at", schedule.next_at}, {"last_at", schedule.last_at}, {"queued", schedule.queued}};
    }
    write_file(root_ / ".stellar" / "endurance" / "schedule.json", root.dump(2) + "\n");
}

} // namespace raven
