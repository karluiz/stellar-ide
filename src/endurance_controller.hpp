#pragma once

#include "checkpoints.hpp"
#include "claude.hpp"
#include "endurance.hpp"
#include "missions.hpp"
#include "workspace.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace raven {

struct EndurancePreferences {
    std::string kimi_model;
    std::string kimi_effort;
    std::string claude_model;
    std::string claude_effort;
    bool vector_recall = true;
};

class EnduranceController {
public:
    using ChangeCallback = std::function<void()>;
    using NotifyCallback = std::function<void(const std::string&, const std::string&, bool, const std::string&)>;
    using SuccessCallback = std::function<void(const EnduranceRun&)>;

    EnduranceController(Workspace& workspace, CheckpointStore& checkpoints, GitMissionStore& missions,
                        std::filesystem::path knowledge_binary, EndurancePreferences preferences);

    void set_callbacks(ChangeCallback changed, NotifyCallback notify, SuccessCallback succeeded);
    void load();
    void tick(const std::vector<EnduranceWorkflow>& workflows);
    bool start(const EnduranceWorkflow& workflow, const std::string& input, bool scheduled, std::string& error);
    void stop();
    bool resume(const std::string& run_id, std::string& error);
    bool approve(bool approved, std::string& error);
    bool busy() const;
    bool busy_workflow(const std::filesystem::path& workflow_path) const;
    bool has_active_run() const { return active_run_.has_value(); }
    const EnduranceRun* active_run() const { return active_run_ ? &*active_run_ : nullptr; }
    EnduranceRun* active_run() { return active_run_ ? &*active_run_ : nullptr; }
    const std::vector<EnduranceRun>& runs() const { return runs_; }
    const EnduranceRun* find_run(const std::string& id) const;
    const EnduranceScheduleState* schedule(const std::filesystem::path& workflow_path) const;
    void update_schedule(const EnduranceWorkflow& workflow);
    void refresh_preferences(EndurancePreferences preferences) { preferences_ = std::move(preferences); }

private:
    Workspace& workspace_;
    CheckpointStore& checkpoints_;
    GitMissionStore& missions_;
    std::filesystem::path knowledge_binary_;
    EndurancePreferences preferences_;
    EnduranceStore store_;
    std::optional<EnduranceRun> active_run_;
    std::vector<EnduranceRun> runs_;
    std::map<std::string, ClaudeProcess> processes_;
    std::map<std::string, bool> saw_delta_;
    std::map<std::string, EnduranceScheduleState> schedule_;
    ChangeCallback changed_;
    NotifyCallback notify_;
    SuccessCallback succeeded_;
    std::int64_t last_output_save_ = 0;
    bool output_dirty_ = false;

    const EnduranceStep* step(const std::string& id) const;
    RunOptions options(const EnduranceStep& step, const EnduranceStepResult& result, const std::string& prompt) const;
    bool launch(const EnduranceStep& step, std::string& error);
    void finish_process(const std::string& id, bool error, const std::string& detail);
    void finish_run(const std::string& status);
    void schedule_ready();
    void tick_schedule(const std::vector<EnduranceWorkflow>& workflows);
    void record(const std::string& event, bool persist = true);
    void replace_history(const EnduranceRun& run);
    std::string transcript() const;
};

} // namespace raven
