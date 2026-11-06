#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace raven {

struct EnduranceRecurrence {
    bool enabled = false;
    std::string expression = "0 9 * * 1-5";
    std::string timezone = "local";
    std::string overlap = "skip";
    std::string missed_runs = "skip";
};

struct EnduranceStep {
    std::string id;
    std::string type = "agent";
    std::string agent = "orchestrator";
    std::string harness;
    std::string mode = "ask";
    std::string model;
    std::string effort;
    std::string isolation = "workspace";
    std::string prompt;
    std::vector<std::string> needs;
    float x = 0.0f;
    float y = 0.0f;
    int timeout_seconds = 900;
    int retries = 0;
};

struct EnduranceWorkflow {
    std::filesystem::path path;
    std::string name = "Untitled mission";
    std::string description;
    std::string default_harness = "kimi";
    int concurrency = 2;
    std::string input_default;
    bool input_required = true;
    EnduranceRecurrence recurrence;
    std::vector<EnduranceStep> steps;
};

struct EnduranceStepResult {
    std::string status = "ready";
    int attempt = 0;
    std::string session_id;
    std::string output;
    std::string error;
    std::string checkpoint_id;
    std::int64_t started_at = 0;
    std::int64_t finished_at = 0;
    double cost_usd = -1.0;
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
};

struct EnduranceRun {
    std::string id;
    std::string workflow_path;
    std::string workflow_name;
    std::string input;
    std::string status = "ready";
    std::string error;
    std::string isolation = "workspace";
    std::string worktree_mission_id;
    EnduranceWorkflow snapshot;
    std::map<std::string, EnduranceStepResult> steps;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
    std::int64_t finished_at = 0;
    bool scheduled = false;
};

struct EnduranceScheduleState {
    std::int64_t next_at = 0;
    std::int64_t last_at = 0;
    bool queued = false;
};

EnduranceWorkflow endurance_default_workflow();
std::string endurance_workflow_yaml(const EnduranceWorkflow& workflow);
EnduranceWorkflow endurance_parse_workflow(const std::string& source, const std::filesystem::path& path = {});
std::vector<std::string> endurance_validate(const EnduranceWorkflow& workflow);
std::string endurance_render_prompt(const EnduranceRun& run, const EnduranceStep& step);
EnduranceStepResult& endurance_result(EnduranceRun& run, const std::string& step_id);
const EnduranceStepResult& endurance_result(const EnduranceRun& run, const std::string& step_id);
bool endurance_dependencies_succeeded(const EnduranceRun& run, const EnduranceStep& step);
bool endurance_is_terminal(const std::string& status);
void endurance_block_downstream(EnduranceRun& run, const std::string& step_id);
void endurance_reset_for_resume(EnduranceRun& run);
std::int64_t endurance_now();
std::int64_t endurance_next_cron(const std::string& expression, std::int64_t after, const std::string& timezone = "local");

class EnduranceStore {
public:
    explicit EnduranceStore(std::filesystem::path workspace_root);

    const std::filesystem::path& workflows_root() const { return workflows_root_; }
    const std::filesystem::path& runs_root() const { return runs_root_; }
    std::vector<EnduranceWorkflow> load_workflows() const;
    void save_workflow(const EnduranceWorkflow& workflow) const;
    EnduranceWorkflow create_workflow();
    EnduranceWorkflow duplicate_workflow(const EnduranceWorkflow& workflow);

    EnduranceRun create_run(const EnduranceWorkflow& workflow, const std::string& input, bool scheduled = false) const;
    void save_run(const EnduranceRun& run, const std::string& event = {}) const;
    std::vector<EnduranceRun> load_runs() const;
    EnduranceRun load_run(const std::string& id) const;

    std::map<std::string, EnduranceScheduleState> load_schedule() const;
    void save_schedule(const std::map<std::string, EnduranceScheduleState>& state) const;

private:
    std::filesystem::path root_;
    std::filesystem::path workflows_root_;
    std::filesystem::path runs_root_;
    std::filesystem::path run_directory(const std::string& id) const;
};

} // namespace raven
